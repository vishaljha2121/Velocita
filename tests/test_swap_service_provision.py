"""Offline provisioning checks; never changes host identities or directories."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('provision', ROOT / 'deploy/v2/swap-runtime/provision_services.py')
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)


class ProvisionTests(unittest.TestCase):
    def empty(self):
        return [], [], {path: None for path in P.DIRECTORIES}

    def complete(self):
        return [dict(x) for x in P.USERS], [dict(x) for x in P.GROUPS], dict(P.DIRECTORIES)

    def test_empty_plan_is_bounded_and_contains_no_launch_or_credential_actions(self):
        result = P.plan(*self.empty())
        self.assertEqual(len(result['actions']), 2 + 2 + len(P.DIRECTORIES))
        self.assertEqual([a['kind'] for a in result['actions'][:4]], ['group', 'group', 'user', 'user'])
        self.assertFalse(result['live_admitted'])
        self.assertFalse(result['credentials_installed'])
        self.assertEqual(result['runtime_directories'], 'systemd creates at service start')
        self.assertTrue(all(a['kind'] in ('group', 'user', 'directory') for a in result['actions']))

    def test_exact_existing_layout_is_idempotent(self):
        self.assertEqual(P.plan(*self.complete())['actions'], [])

    def test_uid_name_and_group_collisions_refused(self):
        for field, value in [('name', 'unrelated'), ('uid', 1003), ('gid', 1004),
                             ('home', '/home/velocitas'), ('shell', '/bin/bash')]:
            users, groups, dirs = self.complete(); users[0][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(P.Refused, 'identity'):
                P.plan(users, groups, dirs)
        users, groups, dirs = self.complete(); groups[0]['members'] = ['vishal']
        with self.assertRaisesRegex(P.Refused, 'group'): P.plan(users, groups, dirs)

    def test_unexpected_supplementary_or_primary_memberships_refused(self):
        for extra in ({'name': 'sudo', 'gid': 27, 'members': [P.USERS[0]['name']]},
                      {'name': 'velocitas', 'gid': 1004, 'members': [P.USERS[1]['name']]}):
            users, groups, dirs = self.complete(); groups.append(extra)
            with self.assertRaisesRegex(P.Refused, 'membership'): P.plan(users, groups, dirs)
        users, groups, dirs = self.complete()
        users.append(dict(name='other', uid=19001, gid=P.GROUPS[1]['gid'], shell='/bin/sh', home='/home/other'))
        with self.assertRaisesRegex(P.Refused, 'membership'): P.plan(users, groups, dirs)

    def test_existing_directory_mode_owner_or_type_refused_without_repair(self):
        for field, value in [('uid', 1003), ('gid', 1004), ('mode', 0o777), ('type', 'symlink')]:
            users, groups, dirs = self.complete()
            path = '/etc/velocita-swap/gateway'
            dirs[path] = dict(dirs[path]); dirs[path][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(P.Refused, 'directory'):
                P.plan(users, groups, dirs)

    def test_apply_requires_root_linux_and_exact_host_before_mutation(self):
        for uid, platform, host in [(1002, 'linux', P.HOST), (0, 'darwin', P.HOST), (0, 'linux', 'other')]:
            with patch.object(P.os, 'geteuid', return_value=uid), patch.object(P.os, 'getuid', return_value=uid), \
                 patch.object(P.sys, 'platform', platform), patch.object(P.socket, 'gethostname', return_value=host), \
                 patch.object(P, 'snapshot', return_value=self.complete()) as snap:
                with self.assertRaises(P.Refused): P.apply()
                snap.assert_not_called()

    def test_apply_revalidates_before_every_action_and_stops_on_first_error(self):
        result = P.plan(*self.empty())
        with patch.object(P, 'require_host_root'), patch.object(P, 'snapshot', return_value=self.empty()) as snap, \
             patch.object(P, 'perform', side_effect=P.Refused('failure')) as mutate:
            with self.assertRaisesRegex(P.Refused, 'failure'): P.apply()
            self.assertGreaterEqual(snap.call_count, 2)
            mutate.assert_called_once_with(result['actions'][0])

    def test_default_cli_only_reviews(self):
        with patch.object(P.sys, 'argv', ['provision_services.py']), patch.object(P.sys, 'platform', 'linux'), \
             patch.object(P.socket, 'gethostname', return_value=P.HOST), \
             patch.object(P, 'snapshot', return_value=self.empty()), patch.object(P, 'apply') as apply, \
             patch('builtins.print') as output:
            P.main()
            apply.assert_not_called()
            self.assertIn('"live_admitted": false', output.call_args.args[0])

    def test_idempotent_apply_never_performs_mutation(self):
        with patch.object(P, 'require_host_root'), patch.object(P, 'snapshot', return_value=self.complete()), \
             patch.object(P, 'perform') as perform:
            result = P.apply()
            self.assertEqual(result['actions_applied'], 0)
            self.assertTrue(result['verified'])
            perform.assert_not_called()

    def test_directory_traversal_uses_nofollow_and_refuses_writable_ancestor(self):
        from types import SimpleNamespace
        root = SimpleNamespace(st_uid=0, st_gid=0, st_mode=0o40755)
        unsafe = SimpleNamespace(st_uid=0, st_gid=0, st_mode=0o40777)
        with patch.object(P.os, 'open', side_effect=[3, 4, 5]) as opened, \
             patch.object(P.os, 'close') as closed, patch.object(P.os, 'fstat', side_effect=[root, unsafe]):
            with self.assertRaisesRegex(P.Refused, 'unsafe root ancestor'):
                P.open_directory('/var/lib')
            self.assertTrue(all(c.args[1] & P.os.O_NOFOLLOW for c in opened.call_args_list))
            closed.assert_any_call(5)

    def test_existing_directory_race_never_chowns_it(self):
        action = {'kind': 'directory', 'path': '/etc/velocita-swap/public', **P.DIRECTORIES['/etc/velocita-swap/public']}
        with patch.object(P, 'open_directory', return_value=3), patch.object(P.os, 'close'), \
             patch.object(P.os, 'mkdir', side_effect=FileExistsError), patch.object(P.os, 'fchown') as chown:
            with self.assertRaises(FileExistsError): P.create_directory(action)
            chown.assert_not_called()

    def test_commands_use_fixed_absolute_tools_and_no_home_creation(self):
        commands = [P.command({'kind': 'user', **u}) for u in P.USERS]
        for cmd in commands:
            self.assertEqual(cmd[0], '/usr/sbin/useradd')
            self.assertIn('--system', cmd)
            self.assertIn('--no-create-home', cmd)
            self.assertIn('--no-user-group', cmd)
            self.assertIn('/usr/sbin/nologin', cmd)
            self.assertNotIn('usermod', cmd)
            self.assertNotIn('systemctl', cmd)


if __name__ == '__main__': unittest.main()
