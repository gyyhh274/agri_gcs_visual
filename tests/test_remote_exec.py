import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('remote_exec',Path(__file__).resolve().parents[1]/'deploy/remote_exec.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class BootstrapModeTests(unittest.TestCase):
    def run_mode(self, password_mode):
        with tempfile.TemporaryDirectory() as directory:
            settings=Path(directory)/'settings.conf'
            settings.write_text('[VideoStream]\nhost=192.168.2.112\nsshUser=ly\nsshUsePassword='+str(password_mode).lower()+'\nsshPassword="test%value"\n')
            identity=Path(directory)/'key';identity.touch()
            with patch.object(sys,'argv',['remote_exec','--settings',str(settings),'--identity',str(identity)]), \
                 patch.object(module,'askpass_helper',return_value=Path(directory)/'askpass'), \
                 patch.object(module,'write_password_file',return_value=Path(directory)/'password') as password_file, \
                 patch.object(module.subprocess,'run',return_value=subprocess.CompletedProcess([],0,stdout='ready\n')) as run:
                self.assertEqual(module.main(),0)
                argv=run.call_args.args[0]
                self.assertEqual(argv[-2],'ly@192.168.2.112')
                self.assertNotIn('test%value',' '.join(argv))
                if password_mode:
                    self.assertEqual(argv[-1],'systemctl --user start agri-task-gateway.service')
                    password_file.assert_called_once_with('test%value')
                else:
                    self.assertEqual(argv[-1],'start-adapter')
                    password_file.assert_not_called()
    def test_password_login_starts_the_user_service(self):self.run_mode(True)
    def test_restricted_key_keeps_forced_action(self):self.run_mode(False)

if __name__=='__main__':unittest.main()
