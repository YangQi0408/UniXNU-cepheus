#!/usr/bin/env python3
"""Provision the optional control key into a mounted temporary iOS root.

The default boot configuration keeps control disabled. This command requires an
explicit --enable and writes fresh random bytes with root-only permissions; it
never prints or stores the key in a report. Run it only against a staging root,
never the host filesystem or a reference source tree.
"""
import argparse
import errno
import os
from pathlib import Path
import secrets
import stat

KEY = Path("private/var/db/sweet-fastboot/control.key")
_DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW


def checked_root(root: Path) -> Path:
    root = root.resolve()
    if root == Path("/"):
        raise ValueError("refusing to operate on host root")
    try:
        root_fd = os.open(root, _DIR_FLAGS)
        marker_fd = os.open("System/Library/xpc/launchd.plist",
                            os.O_RDONLY | os.O_NOFOLLOW, dir_fd=root_fd)
        marker_stat = os.fstat(marker_fd)
    except OSError as exc:
        raise ValueError("not a mounted iOS root (refusing to touch the host)") from exc
    finally:
        if "marker_fd" in locals():
            os.close(marker_fd)
        if "root_fd" in locals():
            os.close(root_fd)
    if not stat.S_ISREG(marker_stat.st_mode):
        raise ValueError("iOS launchd marker is not a regular file")
    return root


def _open_dir(parent_fd: int, name: str, *, create: bool) -> int:
    try:
        return os.open(name, _DIR_FLAGS, dir_fd=parent_fd)
    except FileNotFoundError:
        if not create:
            raise
        os.mkdir(name, 0o700, dir_fd=parent_fd)
        return os.open(name, _DIR_FLAGS, dir_fd=parent_fd)


def _open_key_parent(root: Path, *, create: bool) -> tuple[int, list[int]]:
    """Open KEY's parent through directory descriptors, refusing symlinks."""
    root = checked_root(root)
    fds = [os.open(root, _DIR_FLAGS)]
    try:
        current = fds[0]
        for component in KEY.parent.parts:
            current = _open_dir(current, component, create=create)
            fds.append(current)
        return fds[-1], fds
    except Exception:
        for fd in reversed(fds):
            os.close(fd)
        raise


def _close_fds(fds: list[int]) -> None:
    for fd in reversed(fds):
        os.close(fd)


def provision(root: Path) -> None:
    if os.geteuid() != 0:
        raise ValueError("provisioning requires root for root-only ownership")
    parent_fd, fds = _open_key_parent(root, create=True)
    try:
        try:
            os.stat(KEY.name, dir_fd=parent_fd, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise FileExistsError("control key already exists; refusing replacement")
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
        key_fd = os.open(KEY.name, flags, 0o600, dir_fd=parent_fd)
        try:
            data = secrets.token_bytes(32)
            written = os.write(key_fd, data)
            if written != len(data):
                raise OSError(errno.EIO, "short control-key write")
            os.fsync(key_fd)
            os.fchown(key_fd, 0, 0)
            os.fchmod(key_fd, 0o600)
        finally:
            os.close(key_fd)
        os.fchown(parent_fd, 0, 0)
        os.fchmod(parent_fd, 0o700)
    finally:
        _close_fds(fds)


def verify(root: Path) -> None:
    parent_fd, fds = _open_key_parent(root, create=False)
    try:
        key_fd = os.open(KEY.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent_fd)
        try:
            parent_metadata = os.fstat(parent_fd)
            if (parent_metadata.st_uid, parent_metadata.st_gid,
                    stat.S_IMODE(parent_metadata.st_mode)) != (0, 0, 0o700):
                raise ValueError("control-key directory ownership or mode is unsafe")
            metadata = os.fstat(key_fd)
            if (metadata.st_uid, metadata.st_gid, stat.S_IMODE(metadata.st_mode)) != (0, 0, 0o600):
                raise ValueError("control key ownership or mode is unsafe")
            data = b""
            while len(data) < 33:
                chunk = os.read(key_fd, 33 - len(data))
                if not chunk:
                    break
                data += chunk
        finally:
            os.close(key_fd)
    finally:
        _close_fds(fds)
    if len(data) != 32:
        raise ValueError("control key has invalid length")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("provision", "verify"))
    parser.add_argument("root", type=Path)
    parser.add_argument("--enable", action="store_true",
                        help="required for provisioning; boot remains disabled otherwise")
    args = parser.parse_args()
    if args.action == "provision":
        if not args.enable:
            parser.error("provision requires explicit --enable")
        provision(args.root)
    else:
        verify(args.root)
        print("control key metadata: valid")


if __name__ == "__main__":
    main()
