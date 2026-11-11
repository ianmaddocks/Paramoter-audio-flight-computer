# PlatformIO extra script (all envs):
#  - generates BuildVersion.h: PV_STAMP = build time (yyyymmdd-hhmmss, local), PV_GIT = git
#    describe, so the web page and boot log show which build is installed
#  - for envs with "upload_protocol = custom", uploads firmware.bin / littlefs.bin over Wi-Fi
#    to the vario's /api/ota endpoint (the same one the phone page uses)
Import("env", "projenv")

import datetime
import hashlib
import http.client
import os
import socket
import subprocess
import time
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
    text = ('#pragma once\n#define PV_STAMP "%s"\n#define PV_GIT "%s"\n'
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
    print("Uploading %s (%d bytes) to %s over Wi-Fi..." % (name, len(data), host))
    for attempt in range(3):
        error = send_image(host, md5, fs, boundary, body)
        if not error:
            print("Done - the vario is restarting.")
            return 0
        print("OTA failed: %s" % error)
        if "HTTP 401" in error:
            break  # wrong password: trying again cannot help
        show_vario_log(host)
        if attempt < 2:
            print("Trying again in 8 s...")
            time.sleep(8)
    return 1


def send_image(host, md5, fs, boundary, body):
    """One upload attempt. Returns None on success, else what went wrong."""
    sent = 0
    # Sent a packet at a time with short pauses rather than as fast as the PC can: the vario has
    # little memory for incoming packets, and a burst it cannot hold makes the upload time out.
    try:
        conn = http.client.HTTPConnection(host, 80, timeout=180)
        conn.putrequest("POST", "/api/ota?md5=%s%s" % (md5, "&target=fs" if fs else ""))
        conn.putheader("Content-Type", "multipart/form-data; boundary=" + boundary)
        conn.putheader("Content-Length", str(len(body)))
        conn.putheader("X-OTA-Password", OTA_PASSWORD)
        conn.endheaders()
        for i in range(0, len(body), 1400):
            conn.send(body[i:i + 1400])
            sent = i
            time.sleep(0.03)  # about 45 kB/s: a firmware image takes half a minute
        r = conn.getresponse()
        text = r.read().decode(errors="replace")
        conn.close()
        if r.status != 200:
            return "HTTP %d %s" % (r.status, text)
        print(text)
        return None
    except Exception as e:
        return "%s (after %d of %d bytes)" % (e, sent, len(body))


def show_vario_log(host):
    """After a failed upload: the vario's own account of it, from its debug log."""
    for attempt in range(4):
        try:
            with urllib.request.urlopen("http://%s/api/debuglog?since=0" % host, timeout=6) as r:
                lines = r.read().decode(errors="replace").splitlines()
            shown = [l for l in lines if " OTA:" in l or " MEM:" in l or " E " in l or " WEB:" in l][-8:]
            for line in shown:
                print("  vario: " + line)
            if not shown:
                print("  vario: (nothing about the upload in its log)")
            return
        except Exception as e:
            last = e
            time.sleep(2)
    print("  vario: log not readable (%s)" % last)


if env.GetProjectOption("upload_protocol", "") == "custom":
    env.Replace(UPLOADCMD=ota_upload)
