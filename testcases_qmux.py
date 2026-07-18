from typing import List

from result import TestResult
from testcase import TestCase

KB = 1 << 10
MB = 1 << 20


class TestCaseQMux(TestCase):
    """QMux runs over TLS/TCP, so QUIC pcap handshake heuristics do not apply."""

    @staticmethod
    def additional_envs() -> List[str]:
        # The simulator's WAITFORSERVER probe speaks QUIC over UDP.
        # Clear it so QMux/TCP servers are not gated on a Version Negotiation response.
        return ["WAITFORSERVER="]

    def _check_files(self) -> bool:
        return super()._check_files(
            download_dir=self.client_download_dir(), files=self._files
        )

    def get_paths(self):
        return [self.urlprefix() + p for p in self.get_paths_raw()]

    def get_paths_raw(self):
        return super().get_paths_raw()


class TestCaseHandshake(TestCaseQMux):
    @staticmethod
    def name():
        return "handshake"

    @staticmethod
    def abbreviation():
        return "H"

    @staticmethod
    def desc():
        return "QMux handshake over TLS completes and transfers a small file."

    def get_paths_raw(self):
        self._files = [self._generate_random_file(1 * KB)]
        return self._files

    def check(self) -> TestResult:
        super().check()
        if not self._check_files():
            return TestResult.FAILED
        return TestResult.SUCCEEDED


class TestCaseTransfer(TestCaseQMux):
    @staticmethod
    def name():
        return "transfer"

    @staticmethod
    def abbreviation():
        return "DC"

    @staticmethod
    def desc():
        return "QMux transfers multiple files concurrently with flow control."

    def get_paths_raw(self):
        self._files = [
            self._generate_random_file(2 * MB),
            self._generate_random_file(3 * MB),
            self._generate_random_file(5 * MB),
        ]
        return self._files

    def check(self) -> TestResult:
        super().check()
        if not self._check_files():
            return TestResult.FAILED
        return TestResult.SUCCEEDED


class TestCaseHTTP3(TestCaseQMux):
    @staticmethod
    def name():
        return "http3"

    @staticmethod
    def abbreviation():
        return "3"

    @staticmethod
    def desc():
        return "HTTP/3 over QMux transfers multiple files."

    def get_paths_raw(self):
        self._files = [
            self._generate_random_file(100 * KB),
            self._generate_random_file(100 * KB),
            self._generate_random_file(100 * KB),
        ]
        return self._files

    def check(self) -> TestResult:
        super().check()
        if not self._check_files():
            return TestResult.FAILED
        return TestResult.SUCCEEDED


TESTCASES_QMUX = [
    TestCaseHandshake,
    TestCaseTransfer,
    TestCaseHTTP3,
]
