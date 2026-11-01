#!/usr/bin/env python3
"""Stage a caller-owned key into a new header in a private XNU copy.

The key path is never printed or placed in argv/environment. The generated
header contains an extractable firmware secret; never publish the binary.
"""
import argparse
import os
from pathlib import Path
import stat

HEADER = """/* PRIVATE generated file: do not publish or archive. */
#ifndef CEPHEUS_FASTBOOT_PRIVATE_H
#define CEPHEUS_FASTBOOT_PRIVATE_H
#define CEPHEUS_FASTBOOT_PROVISIONED 1
static const unsigned char cepheus_fastboot_key[32] = { %s };
#endif
"""
DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW


def open_directory_path(path: Path) -> tuple[int, list[int]]:
    if not path.is_absolute():
        raise ValueError("paths must be absolute")
    fds = [os.open("/", DIR_FLAGS)]
    try:
        for name in path.parts[1:]:
            if name in ("", ".", ".."):
                raise ValueError("path traversal is not allowed")
            fds.append(os.open(name, DIR_FLAGS, dir_fd=fds[-1]))
        return fds[-1], fds
    except Exception:
        for fd in reversed(fds): os.close(fd)
        raise


def read_private_key(path: Path) -> bytes:
    path = path.absolute()
    parent_fd, fds = open_directory_path(path.parent)
    try:
        fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent_fd)
        try:
            metadata = os.fstat(fd)
            if (not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1 or
                    metadata.st_uid != os.geteuid() or stat.S_IMODE(metadata.st_mode) & 0o077):
                raise ValueError("key must be a caller-owned, single-link regular file with private mode")
            data = bytearray()
            while len(data) < 33:
                chunk = os.read(fd, 33 - len(data))
                if not chunk: break
                data.extend(chunk)
        finally:
            os.close(fd)
    finally:
        for item in reversed(fds): os.close(item)
    if len(data) != 32:
        raise ValueError("key must contain exactly 32 bytes")
    return bytes(data)


def stage(key_path: Path, private_xnu: Path) -> None:
    root = private_xnu.absolute()
    root_fd, root_fds = open_directory_path(root)
    try:
        if root == Path("/"):
            raise ValueError("refusing filesystem root")
        for ancestor in (root, *root.parents):
            if os.path.lexists(ancestor / ".git"):
                raise ValueError("refusing a repository tree; select a private staging copy")
        root_stat = os.fstat(root_fd)
        if root_stat.st_uid != os.geteuid() or stat.S_IMODE(root_stat.st_mode) & 0o022:
            raise ValueError("private XNU root must be caller-owned and not group/world writable")
        bsd_fd = os.open("bsd", DIR_FLAGS, dir_fd=root_fd)
        try:
            bsd_stat = os.fstat(bsd_fd)
            if bsd_stat.st_uid != os.geteuid() or stat.S_IMODE(bsd_stat.st_mode) & 0o022:
                raise ValueError("private BSD directory ownership or mode is unsafe")
            conf_fd = os.open("conf", DIR_FLAGS, dir_fd=bsd_fd)
            try:
                files_fd = os.open("files", os.O_RDONLY | os.O_NOFOLLOW, dir_fd=conf_fd)
                os.close(files_fd)
            finally: os.close(conf_fd)
            dev_fd = os.open("dev", DIR_FLAGS, dir_fd=bsd_fd)
            try:
                dev_stat = os.fstat(dev_fd)
                if dev_stat.st_uid != os.geteuid() or stat.S_IMODE(dev_stat.st_mode) & 0o022:
                    raise ValueError("private device source directory ownership or mode is unsafe")
                for marker in ("sweet_usb.c",):
                    fd = os.open(marker, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=dev_fd)
                    try:
                        metadata = os.fstat(fd)
                        if not stat.S_ISREG(metadata.st_mode) or metadata.st_size == 0:
                            raise ValueError("private XNU staging source is not a regular file")
                        content = os.read(fd, min(metadata.st_size, 1 << 20))
                        if b"sweet_usb_gadget_start" not in content or b"ud_if_create" not in content:
                            raise ValueError("private XNU source anchor is missing")
                    finally: os.close(fd)
                data = read_private_key(key_path)
                header = (HEADER % ", ".join(f"0x{value:02x}" for value in data)).encode()
                try:
                    out_fd = os.open("cepheus_fastboot_private.h",
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600,
                        dir_fd=dev_fd)
                except FileExistsError as exc:
                    raise ValueError("refusing to replace a private key header") from exc
                complete = False
                try:
                    view = memoryview(header)
                    while view:
                        written = os.write(out_fd, view)
                        if written <= 0: raise OSError("short private header write")
                        view = view[written:]
                    os.fchmod(out_fd, 0o600)
                    os.fsync(out_fd)
                    complete = True
                finally:
                    os.close(out_fd)
                    if not complete:
                        try: os.unlink("cepheus_fastboot_private.h", dir_fd=dev_fd)
                        except FileNotFoundError: pass
                os.fsync(dev_fd)
            finally: os.close(dev_fd)
        finally: os.close(bsd_fd)
    finally:
        for fd in reversed(root_fds): os.close(fd)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("key_file", type=Path)
    parser.add_argument("private_xnu", type=Path)
    args = parser.parse_args()
    stage(args.key_file, args.private_xnu)


if __name__ == "__main__":
    main()
