#!/bin/bash

set -e

CURRENT_DIR=`pwd`
SCRIPTPATH=`cd $(dirname "$0") && pwd`
if [ ! -d $SCRIPTPATH ]; then
    echo "Could not determine absolute dir of $0"
    echo "Maybe accessed with symlink"
fi
SRC_DIR=`cd ${SCRIPTPATH}/.. && pwd`
. "${SCRIPTPATH}/script_include.sh"


if [[ "$OSTYPE" == "darwin"* ]]; then
    USING_OSX=1
fi

if [[ "$1" == "--macos_gcc9" ]]; then
    MACOS_GCC9=1
fi

echo "-> update submodules"
git submodule update --init

# check the .git of the rjit directory
test -d ${SRC_DIR}/.git
IS_GIT_CHECKOUT=$?

if [ $IS_GIT_CHECKOUT -eq 0 ]; then
    echo "-> install git hooks"
    ${SRC_DIR}/tools/install_hooks.sh
fi

function build_r {
    NAME=$1
    R_DIR="${SRC_DIR}/external/${NAME}"

    cd $R_DIR

    if [[ $(git diff --shortstat 2> /dev/null | tail -n1) != "" ]]; then
        echo "** warning: $NAME repo is dirty"
        sleep 1
    fi

    # unpack cache of recommended packages
    cd src/library/Recommended/
    tar xf ../../../../custom-r/cache_recommended.tar
    cd ../../..
    # tools/rsync-recommended || true

    # There is a test that times out due to the compiler triggering in the
    # wrong moment in the matrix package. There doesn't seem to be a good solution
    # other than just patching it.
    cd src/library/Recommended
    tar xzf Matrix_1.2-18.tar.gz
    sed -i -e 's/^stopifnot((st <- system.time(show(M)))\[1\] < 1.0)/((st <- system.time(show(M)))[1] < 1.0)/' Matrix/man/printSpMatrix.Rd
    rm Matrix_1.2-18.tar.gz
    tar czf Matrix_1.2-18.tar.gz Matrix
    rm -rf Matrix
    cd ../../../

    if [ ! -f $R_DIR/Makefile ]; then
        echo "-> configure $NAME"
        cd $R_DIR
        if [ $USING_OSX -eq 1 ]; then
            ./configure --enable-R-shlib --with-internal-tzcode --with-ICU=no || cat config.log
        else
            ./configure --with-ICU=no
        fi
    fi

    if [ ! -f $R_DIR/doc/FAQ ]; then
        cd $R_DIR
        touch doc/FAQ
    fi

    if [ ! -f $R_DIR/SVN-REVISION ]; then
        # R must either be built from a svn checkout, or from the tarball generated by make dist
        # this is a workaround to build it from a git mirror
        # see https://github.com/wch/r-source/wiki/Home/6d35777dcb772f86371bf221c194ca0aa7874016#building-r-from-source
        echo -n 'Revision: ' > SVN-REVISION
        # get the latest revision that is not a rir patch
        REV=$(git log --grep "git-svn-id" -1 --format=%B | grep "^git-svn-id" | sed -E 's/^git-svn-id: https:\/\/svn.r-project.org\/R\/[^@]*@([0-9]+).*$/\1/')
        # can fail on shallow checkouts, so let's put the last known there
        if [ "$REV" == "" ]; then
          REV='74948'
        fi
        echo $REV >> SVN-REVISION
        echo -n 'Last Changed Date: ' >> SVN-REVISION
        REV_DATE=$(git log --grep "git-svn-id" -1 --pretty=format:"%ad" --date=iso | cut -d' ' -f1)
        # can fail on shallow checkouts, so let's put the last known there
        if [ "$REV_DATE" == "" ]; then
          REV_DATE='2018-07-02'
        fi
        echo $REV_DATE >> SVN-REVISION

        rm -f non-tarball
    fi

    echo "-> building $NAME"
    make -j8
}

build_r custom-r

LLVM_DIR="${SRC_DIR}/external/llvm-11"
if [ ! -d $LLVM_DIR ]; then
    echo "-> unpacking LLVM"
    cd "${SRC_DIR}/external"
    if [ $USING_OSX -eq 1 ]; then
        F="clang+llvm-11.0.0-x86_64-apple-darwin"
        if [ ! -f "$F" ]; then
            curl -L https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.0/$F.tar.xz > $F.tar.xz 
        fi
        tar xf $F.tar.xz
        ln -s $F llvm-11
    else
        V=`lsb_release -r -s`
        if [ "$V" == "18.04" ]; then
          V="16.04"
        fi
        if [ "$BUILD_LLVM_FROM_SRC" == "1" ]; then
          V=""
        fi
        if [ "$V" == "20.10" ] || [ "$V" == "20.04" ] || [ "$V" == "16.04" ]; then
          MINOR="1"
          # For some reason there is no 11.0.1 download for 20.04
          if [ "$V" == "20.04" ]; then
            MINOR="0"
          fi
          F="clang+llvm-11.0.$MINOR-x86_64-linux-gnu-ubuntu-$V"
          if [ ! -f "$F" ]; then
              curl -L https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.$MINOR/$F.tar.xz > $F.tar.xz
          fi
          tar xf $F.tar.xz
          ln -s $F llvm-11
        else
          F="llvm-11.0.1.src"
          if [ ! -f "$F" ]; then
            curl -L https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.1/$F.tar.xz > $F.tar.xz
          fi
          tar xf $F.tar.xz
          mkdir llvm-11-build && cd llvm-11-build
          cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLLVM_ENABLE_ASSERTIONS=1 -DLLVM_OPTIMIZED_TABLEGEN=1 -DLLVM_TARGETS_TO_BUILD="X86" ../$F
          ninja
          cd ..
          ln -s llvm-11-build llvm-11
        fi
    fi
fi
