"""Bound transient Windows sharing violations during native fixture teardown."""
import os
import tempfile
import time


class FixtureDirectory(tempfile.TemporaryDirectory):
    def cleanup(self):
        deadline = time.monotonic()+3
        while True:
            try:
                return super().cleanup()
            except OSError as error:
                if os.name != 'nt' or getattr(error, 'winerror', None) not in (32, 33) or time.monotonic() >= deadline:
                    raise
                time.sleep(.05)


if __name__ == '__main__':
    import unittest
    from unittest.mock import patch

    class CleanupTests(unittest.TestCase):
        def error(self, code):
            error = PermissionError('fixture sharing violation')
            error.winerror = code
            return error

        def test_transient_sharing_failure_is_retried(self):
            fixture = object.__new__(FixtureDirectory)
            with patch.object(os, 'name', 'nt'), patch.object(tempfile.TemporaryDirectory, 'cleanup', side_effect=[self.error(32), None]) as cleanup, patch.object(time, 'sleep'):
                fixture.cleanup()
                self.assertEqual(cleanup.call_count, 2)

        def test_persistent_failure_remains_a_failure(self):
            fixture = object.__new__(FixtureDirectory)
            with patch.object(os, 'name', 'nt'), patch.object(tempfile.TemporaryDirectory, 'cleanup', side_effect=self.error(32)), patch.object(time, 'monotonic', side_effect=[0, 4]):
                with self.assertRaises(PermissionError):
                    fixture.cleanup()

        def test_other_permission_failures_are_not_hidden(self):
            fixture = object.__new__(FixtureDirectory)
            with patch.object(os, 'name', 'nt'), patch.object(tempfile.TemporaryDirectory, 'cleanup', side_effect=self.error(5)):
                with self.assertRaises(PermissionError):
                    fixture.cleanup()

    unittest.main()
