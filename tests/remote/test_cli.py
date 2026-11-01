"""Remote client: answers, exit codes, and a missing loader."""

from __future__ import annotations

import contextlib
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from ngb_tools.remote import cli


class FakeChannel(io.RawIOBase):
    """Answers each line written with a canned reply."""

    def __init__(self, replies: list[bytes]) -> None:
        self.replies = replies
        self.pending = b""
        self.written: list[bytes] = []

    def readable(self) -> bool:
        return True

    def writable(self) -> bool:
        return True

    def write(self, data: bytes) -> int:  # type: ignore[override]
        self.written.append(bytes(data))
        self.pending += self.replies.pop(0)
        return len(data)

    def read(self, size: int = -1) -> bytes:
        out, self.pending = self.pending[:size], self.pending[size:]
        return out


class RemoteTestCase(unittest.TestCase):
    def run_cli(self, *argv: str, replies: list[bytes]) -> tuple[int, str, list[bytes]]:
        channel = FakeChannel(replies)
        out = io.StringIO()
        with mock.patch("builtins.open", return_value=channel), contextlib.redirect_stdout(out):
            code = cli.main(list(argv))
        return code, out.getvalue(), channel.written

    def test_sends_each_command_and_prints_answers(self) -> None:
        code, out, written = self.run_cli("status", "god on", replies=[b"ok polls 3\n", b"ok\n"])
        self.assertEqual(code, 0)
        self.assertEqual(written, [b"status\n", b"god on\n"])
        self.assertIn("status -> ok polls 3", out)

    def test_an_error_answer_is_exit_1(self) -> None:
        code, _, _ = self.run_cli("bogus", replies=[b"error unknown command 'bogus' (help)\n"])
        self.assertEqual(code, 1)

    def test_no_loader_is_exit_2(self) -> None:
        missing = str(Path(tempfile.gettempdir()) / f"no-such-pipe-{os.getpid()}")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = cli.main(["--pipe", missing, "status"])
        self.assertEqual(code, 2)
        self.assertIn("no loader listening", err.getvalue())


if __name__ == "__main__":
    unittest.main()
