"""Owner-run, halted-only executable upgrade; retains the existing journals, credentials and driver state."""
import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('strategy_grant_upgrade', HERE.parents[1]/'route-fix/owner_upgrade_restart.py')
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)
COMMIT = 'ba299ff6c89392177cfd4702fda11dabb7ee1719'
BINARY = '9aa06eb11b23f30db37f8c628a2ca4efac7ea32d9b08c21f08a1a95df7942023'
M.COMMIT = COMMIT
M.OLD_COMMIT = 'a835deb24a001adc19ffca5adad83a94d3b4ccad'
M.OLD_MANIFEST = 'e328a6e388f9b1e532edeb62dd41af8866905505bb586c051dcf22b15ded880d'
M.ARCHIVE = Path('/var/lib/velocita-strategy-grant-upgrade-ba299ff')
base_configure = M.configure
base_health = M.health


def configure(binary):
    if binary != BINARY:
        raise ValueError('exact native-qualified ba299ff gateway required')
    base_configure(binary)


def health():
    rows = base_health()
    if len(rows) != 3 or any(row['strategy_mode'] != 5 for row in rows):
        raise ValueError('all three voters must be owner-halted before upgrade or service start')
    return rows


M.configure = configure
M.health = health


if __name__ == '__main__':
    try:
        M.main()
    except Exception as error:
        print(M.json.dumps(dict(error_type=type(error).__name__, error=str(error)[:200], stop_and_inspect=True, do_not_retry=True)))
        raise SystemExit(1)
