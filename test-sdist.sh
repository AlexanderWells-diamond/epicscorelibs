#!/bin/sh
set -e -x

VENV="$1"
[ "$PYTHON" ] || PYTHON=python

[ -f setup.py ] || exit 1

TDIR=`mktemp -d`
trap 'rm -rf $TDIR' INT TERM QUIT EXIT

install -d "$TDIR"/dist

[ "$VENV" ] || VENV="$TDIR"/env
[ -d "$VENV" ] && rm -rf "$VENV"

$PYTHON -m virtualenv --system-site-packages "$VENV"

. "$VENV"/bin/activate

if [ -d "$HOME"/projects/setuptools-dso ]
then
    $PYTHON -m pip install file://"$HOME"/projects/setuptools-dso
else
    $PYTHON -m pip install setuptools-dso
fi

$PYTHON setup.py sdist -d "$TDIR"/dist
ls "$TDIR"/dist/*

$PYTHON -m pip install -v "$TDIR"/dist/*

$PYTHON -m nose epicscorelibs

echo "Success"