# PlatformIO extra script (all envs):
#  - generates BuildVersion.h: PV_VERSION = build time (yyyymmdd-hhmmss, local), PV_GIT = git
#    describe, so the web page and boot log show which build is installed
#  - for envs with "upload_protocol = custom", uploads firmware.bin / littlefs.bin over Wi-Fi
#    to the vario's /api/ota endpoint (the same one the phone page uses)
Import("env", "projenv")

import datetime
import hashlib
import os
import socket
import subprocess
import urllib.error
import urllib.request
import uuid


def git_version():
    try:
        out = subprocess.check_output(["git", "describe", "--always", "--dirty"],
                                      cwd=env.subst("$PROJECT_DIR"), stderr=subprocess.DEVNULL)
        return out.decode().strip()
    except Exception:
        return "unknown"


# A header (not a -D flag) so only the files that include it recompile when the stamp changes.
def write_version_header():
    gen_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
    os.makedirs(gen_dir, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    text = ('#pragma once\n#define PV_VERSION "%s"\n#define PV_GIT "%s"\n'
            % (stamp, git_version()))
    with open(os.path.join(gen_dir, "BuildVersion.h"), "w", newline="\n") as f:
        f.write(text)
    projenv.Append(CPPPATH=[gen_dir])
    print("Firmware version %s" % stamp)


write_version_header()

# Single source for the update password: custom_ota_password in platformio.ini.
OTA_PASSWORD = env.GetProjectOption("custom_ota_password", "")
projenv.Append(CPPDEFINES=[("OTA_PASSWORD", env.StringifyMacro(OTA_PASSWORD))])


AP_ADDRESS = "192.168.4.1"  # the vario's own access point


def find_vario(hosts):
    """First host answering on port 80: the server-network name, then the vario's own AP."""
    for host in hosts:
        try:
            socket.create_connection((host, 80), timeout=4).close()
            return host
        except OSError:
            pass
    return None


def ota_upload(source, target, env):
    path = str(source[0])
    name = os.path.basename(path)
    fs = name.startswith(("littlefs", "spiffs"))
    # The vario's address lives in custom_ota_host, not upload_port: the espressif32 platform
    # switches any IP / .local upload_port to espota and prints a warning.
    hosts = (env.subst("$UPLOAD_PORT"), env.GetProjectOption("custom_ota_host", ""), AP_ADDRESS)
    hosts = list(dict.fromkeys(h for h in hosts if h))
    host = find_vario(hosts)
    if not host:
        print("OTA failed: vario not found at %s.\nJoin the vario's Wi-Fi, or set its server "
              "network in the web page (Wi-Fi settings) and power it on near this PC."
              % " or ".join(hosts))
        return 1
    with open(path, "rb") as f:
        data = f.read()
    md5 = hashlib.md5(data).hexdigest()
    boundary = uuid.uuid4().hex
    body = (("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n") % (boundary, name)).encode()
    body += data + ("\r\n--%s--\r\n" % boundary).encode()
    url = "http://%s/api/ota?md5=%s%s" % (host, md5, "&target=fs" if fs else "")
    req = urllib.request.Request(url, data=body, headers={
        "Content-Type": "multipart/form-data; boundary=" + boundary,
        "X-OTA-Password": OTA_PASSWORD})
    print("Uploading %s (%d bytes) to %s over Wi-Fi..." % (name, len(data), host))
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            print(r.read().decode(errors="replace"))
    except urllib.error.HTTPError as e:
        print("OTA failed: HTTP %d %s" % (e.code, e.read().decode(errors="replace")))
        return 1
    except Exception as e:
        print("OTA failed: %s" % e)
        return 1
    print("Done - the vario is restarting.")
    return 0


if env.GetProjectOption("upload_protocol", "") == "custom":
    env.Replace(UPLOADCMD=ota_upload)
