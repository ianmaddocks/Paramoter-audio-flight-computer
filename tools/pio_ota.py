# PlatformIO extra script (all envs):
#  - defines PV_VERSION from "git describe" so the web page shows which build is installed
#  - for envs with "upload_protocol = custom", uploads firmware.bin / littlefs.bin over Wi-Fi
#    to the vario's /api/ota endpoint (the same one the phone page uses)
Import("env", "projenv")

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


projenv.Append(CPPDEFINES=[("PV_VERSION", env.StringifyMacro(git_version()))])


AP_ADDRESS = "192.168.4.1"  # the vario's own access point


def find_vario(hosts):
    """First host answering on port 80: the home-network name, then the vario's own AP."""
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
    hosts = list(dict.fromkeys(h for h in (env.subst("$UPLOAD_PORT"), AP_ADDRESS) if h))
    host = find_vario(hosts)
    if not host:
        print("OTA failed: vario not found at %s.\nJoin the vario's Wi-Fi, or set its home "
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
        "Content-Type": "multipart/form-data; boundary=" + boundary})
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
