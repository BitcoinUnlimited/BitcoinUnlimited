#!/usr/bin/env python3
import argparse
import logging
import os
import sys
import shutil
GIT_REPO = "https://github.com/dagurval/electrs.git"
GIT_BRANCH = "v0.5.0bu"
EXPECT_HEAD = "84d449ed283296dfc266433a6919ffcb3ca55344"

ROOT_DIR = os.path.realpath(
        os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
ELECTRS_DIR = os.path.join(ROOT_DIR, "electrs")

parser = argparse.ArgumentParser()
parser.add_argument('--allow-modified', help='Allow building modified/dirty repo',
        action = "store_true")
parser.add_argument('--verbose', help='Sets log level to DEBUG',
        action = "store_true")
args = parser.parse_args()

level = logging.DEBUG if args.verbose else logging.INFO

logging.basicConfig(format = '%(asctime)s.%(levelname)s: %(message)s',
        level=level,
        stream=sys.stdout)

def bail(*args):
    logging.error(*args)
    sys.exit(1)

def check_dependencies():
    v = sys.version_info
    if v[0] < 3 or (v[0] == 3 and v[1] < 3):
        bail("python >= 3.3 required");

    try:
        import git
    except Exception as e:
        logging.error("Failed to 'import git'")
        logging.error("Tip: On Debian/Ubuntu you need to install python3-git")
        bail(str(e))

    import shutil
    if shutil.which("cargo") is None:
        logging.error("Cannot find 'cargo', will not be able to build electrs")
        logging.error("You need to install rust (1.28+) https://rustup.rs/")
        bail("rust not found")

def clone_repo():
    import git
    logging.info("Cloning %s to %s", GIT_REPO, ELECTRS_DIR)
    repo = git.Repo.clone_from(GIT_REPO, ELECTRS_DIR, branch=GIT_BRANCH)

def verify_repo(allow_modified):
    import git
    repo = git.Repo(ELECTRS_DIR)
    if repo.is_dirty():
        logging.error("Validation failed - electrs has local modifications.")
        allow_modified or bail("Bailing")

    if repo.head.object.hexsha != EXPECT_HEAD:
        # TODO: Add command line option to reset HEAD to GIT_BRANCH at EXPECT_HEAD
        logging.error("Validation failed - electrs HEAD differs from expected (%s vs %s)",
                repo.head.object.hexsha, EXPECT_HEAD)
        allow_modified or bail("Bailing")

def output_reader(pipe, queue):
    try:
        with pipe:
            for l in iter(pipe.readline, b''):
                queue.put(l)
    finally:
        queue.put(None)

def cargo_run(args):
    import subprocess
    from threading import Thread
    from queue import Queue

    cargo = shutil.which("cargo")
    args = [cargo] + args
    logging.info("Running %s", args)
    assert cargo is not None

    p = subprocess.Popen(args, cwd = ELECTRS_DIR,
        stdout = subprocess.PIPE, stderr = subprocess.PIPE)

    q = Queue()
    Thread(target = output_reader, args = [p.stdout, q]).start()
    Thread(target = output_reader, args = [p.stderr, q]).start()

    for line in iter(q.get, None):
        logging.info(line.decode('utf-8').rstrip())

    p.wait()
    rc = p.returncode
    assert rc is not None
    if rc != 0:
        bail("cargo failed with return code %s", rc)

check_dependencies()

if not os.path.exists(ELECTRS_DIR):
    clone_repo()
verify_repo(args.allow_modified)

cargo_run(["build", "--release"])
cargo_run(["test", "--release"])

src = os.path.join(ELECTRS_DIR, "target", "release", "electrs")
dst = os.path.join(ROOT_DIR, "src")
logging.info("Copying %s to %s", src, dst)
shutil.copy(src, dst)

logging.info("Done")
