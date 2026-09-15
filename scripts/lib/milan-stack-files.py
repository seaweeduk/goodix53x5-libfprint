#!/usr/bin/env python3
"""Exact-file bookkeeping for the manual source installation (never packages)."""

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys

META = "/usr/share/goodix53x5-milan"
INVENTORY = META + "/inventory.json"
SUMS = META + "/SHA256SUMS"
OWNED = META + "/ownership.json"
CONTROL = (INVENTORY, SUMS, OWNED)


def fail(message):
    raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_name(name):
    p = PurePosixPath(name)
    if (str(p) != name or ".." in p.parts or any(c.isspace() for c in name)
            or not (name.startswith("/usr/") or name == "/etc/fprintd.conf")):
        fail(f"unsafe inventory path: {name}")


def mapped(root, name):
    safe_name(name)
    path = root / name.lstrip("/")
    # Never follow a substituted directory into unrelated files.
    for parent in path.parents:
        if parent == root:
            break
        if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
            fail(f"unsafe parent directory: {parent}")
    return path


def entry(root, name):
    path = mapped(root, name)
    mode = path.lstat().st_mode
    if stat.S_ISLNK(mode):
        target = os.readlink(path)
        if os.path.isabs(target) or any(c.isspace() for c in target):
            fail(f"unsafe symlink: {name} -> {target}")
        resolved = os.path.normpath(str(PurePosixPath(name).parent / target))
        safe_name(resolved)
        return {"path": name, "target": target}
    if not stat.S_ISREG(mode) or mode & 0o7022:
        fail(f"unsafe file type or permissions: {name}")
    return {"path": name, "sha256": digest(path), "mode": stat.S_IMODE(mode)}


def inventory(root):
    if root.name != "payload":
        fail("inventory requires a bounded publication/payload directory")
    records = []
    # Only a bounded build payload is enumerated; installed roots never are.
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in list(dirs):
            if (Path(parent) / name).is_symlink():
                dirs.remove(name)
                files.append(name)
        for name in files:
            absolute = "/" + str((Path(parent) / name).relative_to(root))
            if absolute not in CONTROL:
                records.append(entry(root, absolute))
    records.sort(key=lambda item: item["path"])
    mapped(root, INVENTORY).write_text(json.dumps(records, indent=2) + "\n")
    sums = [(item["sha256"], item["path"]) for item in records if "sha256" in item]
    sums.append((digest(mapped(root, INVENTORY)), INVENTORY))
    mapped(root, SUMS).write_text("".join(f"{value}  .{name}\n" for value, name in sums))


def load(root, owned=False, complete=False):
    if complete and root.name != "payload":
        fail("build verification requires a bounded publication/payload directory")
    for name in (INVENTORY, SUMS, META + "/build.env") + ((OWNED,) if owned else ()):
        p = mapped(root, name)
        if p.is_symlink() or not p.is_file():
            fail(f"missing or unsafe metadata: {p}")
        if p.stat().st_mode & 0o7022:
            fail(f"unsafe metadata permissions: {p}")
        if owned and p.stat().st_uid != 0:
            fail(f"installation metadata is not root-owned: {p}")
    records = json.loads(mapped(root, INVENTORY).read_text())
    names = set()
    for item in records:
        name = item["path"]
        safe_name(name)
        if name in names or name in CONTROL:
            fail(f"duplicate or reserved inventory path: {name}")
        names.add(name)
        if entry(root, name) != item:
            fail(f"modified installed/staged file: {name}")
        if owned and mapped(root, name).lstat().st_uid != 0:
            fail(f"installed file is no longer root-owned: {name}")
        if "target" in item:
            resolved = mapped(root, name).resolve(strict=True)
            if not resolved.is_relative_to(root) or not resolved.is_file():
                fail(f"symlink leaves payload: {name}")
    if META + "/build.env" not in names:
        fail("build.env is not inventoried")
    expected = "".join(f"{item['sha256']}  .{item['path']}\n" for item in records if "sha256" in item)
    expected += f"{digest(mapped(root, INVENTORY))}  .{INVENTORY}\n"
    if mapped(root, SUMS).read_text() != expected:
        fail("checksum inventory mismatch")
    if owned:
        marker = json.loads(mapped(root, OWNED).read_text())
        if marker != ownership(root):
            fail("installation ownership seal mismatch")
    if complete:
        actual = set()
        for parent, dirs, files in os.walk(root, followlinks=False):
            for name in list(dirs):
                if (Path(parent) / name).is_symlink():
                    dirs.remove(name)
                    files.append(name)
            actual.update("/" + str((Path(parent) / name).relative_to(root)) for name in files)
        if actual != names | {INVENTORY, SUMS}:
            fail("payload contains unrecorded files")
    return records


def ownership(root):
    return {"format": 2, "managed_by": "goodix53x5-milan-stack",
            "inventory_sha256": digest(mapped(root, INVENTORY)),
            "checksums_sha256": digest(mapped(root, SUMS))}


def package_conflicts(names):
    commands = [cmd for cmd in (["pacman", "-Qo", "--"], ["dpkg-query", "-S"],
                                ["rpm", "-qf", "--"]) if shutil.which(cmd[0])]
    if not commands:
        fail("no package ownership query available (pacman, dpkg-query, or rpm)")
    conflicts = []
    for name in sorted(names):
        # Query merged-/usr aliases too: dpkg may record the old /lib path.
        aliases = [name]
        if name.startswith(("/usr/lib/", "/usr/lib64/", "/usr/bin/", "/usr/sbin/")):
            aliases.append(name[4:])
        for cmd in commands:
            for alias in aliases:
                result = subprocess.run(cmd + [alias], capture_output=True, text=True)
                if result.returncode == 0:
                    conflicts.append(f"{alias}: {result.stdout.strip()}")
                elif result.returncode != 1:
                    fail(f"package ownership query failed: {' '.join(cmd)}: {result.stderr.strip()}")
    if conflicts:
        fail("package-owned destination files; resolve/remove these packages first:\n" + "\n".join(conflicts))


def preflight(source, root):
    new = load(source, complete=True)
    marker = mapped(root, OWNED)
    old = load(root, owned=True) if marker.exists() or marker.is_symlink() else []
    old_names = {item["path"] for item in old} | (set(CONTROL) if old else set())
    names = {item["path"] for item in new} | set(CONTROL)
    package_conflicts(names | old_names)
    for name in names:
        path = mapped(root, name)
        if (path.exists() or path.is_symlink()) and name not in old_names:
            fail(f"refusing unmanaged destination: {name}")
    for old_path in ("opt/goodix53x5-milan", "etc/systemd/system/fprintd.service.d/98-goodix53x5-milan-stack.conf"):
        path = root / old_path
        if path.exists() or path.is_symlink():
            fail("old /opt installation detected; run its old uninstall script first")
    return new, old


def selinux_enabled():
    return shutil.which("restorecon") and (
        Path("/sys/fs/selinux/enforce").exists() or
        (shutil.which("selinuxenabled") and
         subprocess.run(["selinuxenabled"], check=False).returncode == 0))


def copy_file(source, root, name, label_parents):
    src, dst = mapped(source, name), mapped(root, name)
    missing = []
    for parent in dst.parents:
        if parent.exists():
            break
        missing.append(parent)
    for parent in reversed(missing):
        parent.mkdir()
        if label_parents:
            # Only directories created by this install, before creating children.
            subprocess.run(["restorecon", "--", str(parent)], check=True)
    if dst.exists() or dst.is_symlink():
        dst.unlink()
    if src.is_symlink():
        dst.symlink_to(os.readlink(src))
    else:
        # copyfile deliberately does not preserve home-directory xattrs/labels.
        shutil.copyfile(src, dst)
        dst.chmod(stat.S_IMODE(src.stat().st_mode))


def main():
    action = sys.argv[1]
    root = Path(sys.argv[2]).resolve()
    if action == "inventory":
        inventory(root)
    elif action == "verify-build":
        load(root, complete=True)
    elif action == "verify-installed":
        records = load(root, owned=True)
        package_conflicts({item["path"] for item in records} | set(CONTROL))
    elif action in ("preflight", "install"):
        source = Path(sys.argv[3]).resolve()
        new, old = preflight(source, root)
        if action == "install":
            label_parents = selinux_enabled()
            names = {item["path"] for item in new}
            for item in new:
                copy_file(source, root, item["path"], label_parents)
            for item in old:
                if item["path"] not in names:
                    mapped(root, item["path"]).unlink()
            for name in (INVENTORY, SUMS):
                copy_file(source, root, name, label_parents)
            mapped(root, OWNED).write_text(json.dumps(ownership(root), indent=2) + "\n")
            mapped(root, OWNED).chmod(0o644)
    elif action == "remove":
        records = load(root, owned=True)
        names = [item["path"] for item in records] + list(CONTROL)
        package_conflicts(set(names))
        for name in names:
            mapped(root, name).unlink()
        # Shared directories and all persistent state are deliberately retained.
    elif action == "relabel":
        if not selinux_enabled():
            return
        records = load(root, owned=True)
        names = {item["path"] for item in records} | set(CONTROL)
        for name in names:
            # Exact installed paths only; shared /usr directories are not relabeled.
            subprocess.run(["restorecon", "--", str(mapped(root, name))], check=True)
    else:
        fail(f"unknown action: {action}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        sys.exit(f"error: {error}")
