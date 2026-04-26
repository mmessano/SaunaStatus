"""Regression checks for route and constant documentation drift."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "verify_doc_drift.py"


def _load_verify_doc_drift_module():
    spec = importlib.util.spec_from_file_location("verify_doc_drift", SCRIPT_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class DocDriftTest(unittest.TestCase):
    def test_doc_drift_verifier_reports_clean_repo_state(self) -> None:
        module = _load_verify_doc_drift_module()
        self.assertEqual(module.run_checks(), [])


if __name__ == "__main__":
    unittest.main()
