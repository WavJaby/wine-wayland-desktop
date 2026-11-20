# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 the wine-wayland-desktop contributors
"""check-wine-source.sh against throw-away git repositories."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / 'scripts/check-wine-source.sh'
GIT_ENV = {**os.environ, 'GIT_AUTHOR_NAME': 't', 'GIT_AUTHOR_EMAIL': 't@example.invalid',
           'GIT_COMMITTER_NAME': 't', 'GIT_COMMITTER_EMAIL': 't@example.invalid'}


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True, text=True,
                          env=GIT_ENV).stdout.strip()


class WineLockTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='wine lock ')
        self.addCleanup(tmp.cleanup)
        self.repo = Path(tmp.name) / 'wine'
        self.repo.mkdir()
        git(self.repo, 'init', '-q')
        (self.repo / 'a.c').write_text('base\n')
        git(self.repo, 'add', 'a.c')
        git(self.repo, 'commit', '-q', '-m', 'base')
        self.base = git(self.repo, 'rev-parse', 'HEAD')
        (self.repo / 'a.c').write_text('ours\n')
        git(self.repo, 'commit', '-q', '-am', 'ours')
        self.lock = Path(tmp.name) / 'wine.lock'
        self.write_lock(commit=git(self.repo, 'rev-parse', 'HEAD'), tree=git(self.repo, 'rev-parse', 'HEAD^{tree}'))

    def write_lock(self, commit, tree, base=None):
        self.lock.write_text(f'# test\ncommit={commit}\ntree={tree}\nbase_commit={base or self.base}\n')

    def check(self):
        return subprocess.run(['bash', str(CHECK), str(self.repo), str(self.lock)], capture_output=True, text=True)

    def test_matching_checkout_passes(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_changed_file_fails(self):
        (self.repo / 'a.c').write_text('changed\n')
        git(self.repo, 'commit', '-q', '-am', 'drift')
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('tree mismatch', result.stderr)

    def test_dirty_worktree_fails(self):
        (self.repo / 'a.c').write_text('uncommitted\n')
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('not clean', result.stderr)

    def test_same_tree_new_commit_passes_with_note(self):
        git(self.repo, 'commit', '-q', '--amend', '-m', 'ours, reworded')
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('differs from locked', result.stderr)

    def test_unrelated_base_fails(self):
        self.write_lock(git(self.repo, 'rev-parse', 'HEAD'), git(self.repo, 'rev-parse', 'HEAD^{tree}'), base='1' * 40)
        result = self.check()
        self.assertEqual(result.returncode, 1)

    def test_malformed_lock_is_a_usage_error(self):
        self.lock.write_text('commit=abc\n')
        self.assertEqual(self.check().returncode, 2)


if __name__ == '__main__':
    unittest.main()
