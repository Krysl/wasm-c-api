###############################################################################
# Configuration

# Change these as you see fit.

V8_VERSION = branch-heads/12.6 # check v8 version in https://chromiumdash.appspot.com/branches
V8_ARCH = x64
# V8_MODE = release
V8_MODE = debug

WASM_FLAGS = -DWASM_API_DEBUG  # -DWASM_API_DEBUG_LOG

C_COMP = clang
PYTHON3 = F:/Python/Python3.12-64/python.exe
WASM_INTERPRETER = ../spec/interpreter/wasm  # Adjust as needed.

CLANG_CL = ${V8_V8}/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe
LLD_LINK = ${V8_V8}/third_party/llvm-build/Release+Asserts/bin/lld-link.exe
MSVC = D:/Program Files/Microsoft Visual Studio/2022/Community/VC
WIN_SDK = C:/Program Files (x86)/Windows Kits/10/include/10.0.22621.0

MSVC_INCLUDES = \
	"-imsvc${MSVC}/Tools/MSVC/14.39.33519/include" \
	"-imsvc${MSVC}/Tools/MSVC/14.39.33519/ATLMFC/include" \
	"-imsvc${MSVC}/Auxiliary/VS/include" \
	"-imsvc${WIN_SDK}/ucrt" \
	"-imsvc${WIN_SDK}/um" \
	"-imsvc${WIN_SDK}/shared" \
	"-imsvc${WIN_SDK}/winrt" \
	"-imsvc${WIN_SDK}/cppwinrt"

# No need to change what follows.

# flags get run v8
_NINJA = ${V8_OUT}/obj/v8_hello_world.ninja
define strlen
$(shell printf '%s' '$1' | wc -c)
endef
define get_def
$(shell printf '%s' '$(strip $(shell cat ${_NINJA} | grep "$1 = "))' | cut -c $(call strlen,$1 = )- | sed 's/$$:/:/g')
endef
DEFINES=$(call get_def,defines)
_INCLUDE_DIRS=$(call get_def,include_dirs)
INCLUDE_DIRS=$(foreach dir,$(foreach dir,$(subst -I,${V8_OUT}/,${_INCLUDE_DIRS}),$(realpath $(dir))),-I$(dir))
CFLAGS=$(call get_def,cflags)
CFLAGS_CC=$(call get_def,cflags_cc)
LIBS=$(call get_def,libs)

# Base directories
V8_DIR = ${abspath v8}
WASM_DIR = .
EXAMPLE_DIR = example
OUT_DIR = out

# Example config
EXAMPLE_OUT = ${OUT_DIR}/${EXAMPLE_DIR}
EXAMPLES = \
  hello \
  callback \
  trap \
  start \
  reflect \
  global \
  memory \
  hostref \
  multi \
  table \
  serialize \
  threads \
  finalize \


# Wasm config
WASM_INCLUDE = ${WASM_DIR}/include
WASM_SRC = ${WASM_DIR}/src
WASM_OUT = ${OUT_DIR}/${WASM_DIR}
WASM_C_LIBS = wasm-bin wasm-c
WASM_CC_LIBS = wasm-bin wasm-v8
WASM_C_O = ${WASM_C_LIBS:%=${WASM_OUT}/%.o}
WASM_CC_O = ${WASM_CC_LIBS:%=${WASM_OUT}/%.o}
WASM_V8_PATCH = wasm-v8-lowlevel

# V8 config
V8_BUILD = ${V8_ARCH}.${V8_MODE}
V8_V8 = ${V8_DIR}/v8
V8_DEPOT_TOOLS = ${V8_DIR}/depot_tools
V8_PATH = "$(abspath ${V8_DEPOT_TOOLS}):${PATH}"
V8_INCLUDE = ${V8_V8}/include
V8_SRC = ${V8_V8}/src
V8_OUT = ${V8_V8}/out.gn/${V8_BUILD}
V8_LIBS = monolith # base libbase external_snapshot libplatform libsampler


VCPKG=$(abspath ${VCPKG_ROOT})
ifdef OS
  IS_WIN = true
  LIBS_EXT = lib
  LIBS_PREFIX = 
  EXEC_EXT = .exe
# need by example/threads.c on Windows
# https://learn.microsoft.com/en-us/vcpkg/get_started/get-started
#    vcpkg install pthreads:x64-windows
  PTHREAD_INCLUDE=${VCPKG}/packages/pthreads_x64-windows/include
  PTHREAD_LIB=${VCPKG}/packages/pthreads_x64-windows/lib/pthreadVC3.lib
else
  ifeq ($(shell uname), Linux)
    LIBS_EXT = a
    LIBS_PREFIX = lib
  endif
  EXEC_EXT = 
  PTHREAD_INCLUDE=
  PTHREAD_LIB=
endif
V8_BLOBS = # natives_blob snapshot_blob
V8_CURRENT = $(shell if [ -f ${V8_OUT}/version ]; then cat ${V8_OUT}/version; else echo ${V8_VERSION}; fi)

V8_GN_ARGS = \
  is_component_build=false \
  v8_static_library=true \
  v8_monolithic=true \
  v8_enable_webassembly=true \
  treat_warnings_as_errors=false \
  v8_use_external_startup_data=false \
  v8_enable_i18n_support=false \
  use_custom_libcxx=false \
  use_custom_libcxx_for_host=false \
  is_debug=true

# Compiler config
ifeq (${C_COMP},clang)
  CC_COMP = clang++${EXEC_EXT}
  LD_GROUP_START = 
  LD_GROUP_END = 
  COMP_FLAGS = -pthread
else ifeq (${C_COMP},gcc)
  CC_COMP = g++
  LD_GROUP_START = -Wl,--start-group
  LD_GROUP_END = -Wl,--end-group
else
  $(error C_COMP set to unknown compiler, must be clang or gcc)
endif

###############################################################################
# Examples
#
# To build Wasm APIs and run all examples:
#   make all
#
# To run only C examples:
#   make c
#
# To run only C++ examples:
#   make cc
#
# To run individual C example (e.g. hello):
#   make run-hello-c
#
# To run individual C++ example (e.g. hello):
#   make run-hello-cc
#
# To rebuild after V8 version change:
#   make clean all

.PHONY: all cc c
all: cc c
c: ${EXAMPLES:%=run-%-c}
cc: ${EXAMPLES:%=run-%-cc}
co: ${EXAMPLES:%=${EXAMPLE_OUT}/%-c.o}
cco: ${EXAMPLES:%=${EXAMPLE_OUT}/%-cc.o}

# Running a C / C++ example
run-%-c: ${EXAMPLE_OUT}/%-c ${EXAMPLE_OUT}/%.wasm ${V8_BLOBS:%=${EXAMPLE_OUT}/%.bin}
	@echo ==== C ${@:run-%-c=%} ====; \
	cd ${EXAMPLE_OUT}; ./${@:run-%=%}
	@echo ==== Done ====
	rm -f ${EXAMPLE_OUT}/${@:run-%=%}.pdb

run-%-cc: ${EXAMPLE_OUT}/%-cc${EXEC_EXT} ${EXAMPLE_OUT}/%.wasm ${V8_BLOBS:%=${EXAMPLE_OUT}/%.bin}
	@echo ==== C++ ${@:run-%-cc=%} ====; \
	cd ${EXAMPLE_OUT}; ./${@:run-%=%${EXEC_EXT}}
	@echo ==== Done ====
	rm -f ${EXAMPLE_OUT}/${@:run-%=%}${EXEC_EXT}.pdb

# Compiling C / C++ example
${EXAMPLE_OUT}/%-c.o: ${EXAMPLE_DIR}/%.c ${WASM_INCLUDE}/wasm.h
	mkdir -p ${EXAMPLE_OUT}
	export MSYS2_ARG_CONV_EXCL=*; \
	${CLANG_CL} \
		/c $< \
		/Fo$@ \
		/nologo \
		${WASM_FLAGS} \
		${MSVC_INCLUDES} \
		${DEFINES} \
		-DWASM_API_DEBUG \
		-D_ITERATOR_DEBUG_LEVEL=0 \
		-I${WASM_INCLUDE} \
		-I${V8_V8} \
		-I${V8_OUT}/gen \
		-I${V8_V8}/include \
		-I${V8_V8}/third_party/abseil-cpp \
		-I${V8_OUT}/gen/include \
		-I${V8_V8}/third_party/fp16/src/include \
		-I${PTHREAD_INCLUDE} \
		${CFLAGS} \
		${CFLAGS_CC} \
		/Fd"$@.pdb" -v;

${EXAMPLE_OUT}/%-cc.o: ${EXAMPLE_DIR}/%.cc ${WASM_INCLUDE}/wasm.hh
	mkdir -p ${EXAMPLE_OUT}
	export MSYS2_ARG_CONV_EXCL=*; \
	${CLANG_CL} \
		/c $< \
		/Fo$@ \
		/nologo \
		${WASM_FLAGS} \
		${MSVC_INCLUDES} \
		${DEFINES} \
		-DWASM_API_DEBUG \
		-D_ITERATOR_DEBUG_LEVEL=0 \
		-I${WASM_INCLUDE} \
		-I${V8_V8} \
		-I${V8_OUT}/gen \
		-I${V8_V8}/include \
		-I${V8_V8}/third_party/abseil-cpp \
		-I${V8_OUT}/gen/include \
		-I${V8_V8}/third_party/fp16/src/include \
		${CFLAGS} \
		${CFLAGS_CC} \
		/Fd"$@.pdb" -v;

# Linking C / C++ example
.PRECIOUS: ${EXAMPLES:%=${EXAMPLE_OUT}/%-c}
${EXAMPLE_OUT}/%-c: ${EXAMPLE_OUT}/%-c.o ${WASM_C_O}
	export MSYS2_ARG_CONV_EXCL=*; \
	${LLD_LINK} \
	"/OUT:$@" \
	/nologo \
	-libpath:../../third_party/llvm-build/Release+Asserts/lib/clang/19/lib/windows \
	"-libpath:${MSVC}/Tools/MSVC/14.39.33519/ATLMFC/lib/x64" \
	"-libpath:${MSVC}/Tools/MSVC/14.39.33519/lib/x64" \
	"-libpath:${WIN_SDK}/ucrt/x64" \
	"-libpath:${WIN_SDK}/um/x64" \
	/MACHINE:X64  \
	"/PDB:$@.pdb" \
	$< ${V8_OUT}/obj/v8_monolith.lib \
	${WASM_C_O} \
	${LIBS} \
	${PTHREAD_LIB} \
	/WX --color-diagnostics /call-graph-profile-sort:no /TIMESTAMP:1714885200 /lldignoreenv /pdbpagesize:16384 /DEBUG:GHASH /FIXED:NO /ignore:4199 /ignore:4221 /NXCOMPAT /DYNAMICBASE /INCREMENTAL /OPT:NOREF /OPT:NOICF /SUBSYSTEM:CONSOLE,10.0 /STACK:2097152 \
	libcmtd.lib

.PRECIOUS: ${EXAMPLES:%=${EXAMPLE_OUT}/%-cc${EXEC_EXT}}
${EXAMPLE_OUT}/%-cc${EXEC_EXT}: ${EXAMPLE_OUT}/%-cc.o ${WASM_CC_O} ${V8_OUT}/obj/v8_monolith.lib
	export MSYS2_ARG_CONV_EXCL=*; \
	${LLD_LINK} \
	"/OUT:$@" \
	/nologo \
	-libpath:../../third_party/llvm-build/Release+Asserts/lib/clang/19/lib/windows \
	"-libpath:${MSVC}/Tools/MSVC/14.39.33519/ATLMFC/lib/x64" \
	"-libpath:${MSVC}/Tools/MSVC/14.39.33519/lib/x64" \
	"-libpath:${WIN_SDK}/ucrt/x64" \
	"-libpath:${WIN_SDK}/um/x64" \
	/MACHINE:X64  \
	"/PDB:$@.pdb" \
	${WASM_CC_O} \
	$< ${V8_OUT}/obj/v8_monolith.lib \
	advapi32.lib comdlg32.lib dbghelp.lib dnsapi.lib gdi32.lib msimg32.lib odbc32.lib odbccp32.lib oleaut32.lib shell32.lib shlwapi.lib user32.lib usp10.lib uuid.lib version.lib wininet.lib winmm.lib winspool.lib ws2_32.lib delayimp.lib kernel32.lib ole32.lib bcrypt.lib \
	/WX --color-diagnostics /call-graph-profile-sort:no /TIMESTAMP:1714885200 /lldignoreenv /pdbpagesize:16384 /DEBUG:GHASH /FIXED:NO /ignore:4199 /ignore:4221 /NXCOMPAT /DYNAMICBASE /INCREMENTAL /OPT:NOREF /OPT:NOICF /SUBSYSTEM:CONSOLE,10.0 /STACK:2097152 \
	libcmtd.lib

# Installing V8 snapshots
.PRECIOUS: ${V8_BLOBS:%=${EXAMPLE_OUT}/%.bin}
${EXAMPLE_OUT}/%.bin: ${V8_OUT}/%.bin
	cp $< $@

# Installing Wasm binaries
.PRECIOUS: ${EXAMPLES:%=${EXAMPLE_OUT}/%.wasm}
${EXAMPLE_OUT}/%.wasm: ${EXAMPLE_DIR}/%.wasm
	cp $< $@

# Assembling Wasm binaries
.PRECIOUS: %.wasm
%.wasm: %.wat
	${WASM_INTERPRETER} -d $< -o $@

${EXAMPLE_OUT}/threads-c:${EXAMPLE_OUT}/pthreadVC3.dll

${EXAMPLE_OUT}/pthreadVC3.dll:
	ln -s ${VCPKG}/packages/pthreads_x64-windows/bin/pthreadVC3.dll $@

###############################################################################
# Wasm C / C++ API
#
# To build both C / C++ APIs:
#   make wasm

.PHONY: wasm wasm-c wasm-cc
wasm: wasm-c wasm-cc
wasm-c: ${WASM_C_LIBS:%=${WASM_OUT}/%.o}
wasm-cc: ${WASM_CC_LIBS:%=${WASM_OUT}/%.o}


# Compiling
${WASM_OUT}/%.o: ${WASM_SRC}/%.cc ${WASM_INCLUDE}/wasm.h ${WASM_INCLUDE}/wasm.hh
	mkdir -p ${WASM_OUT}
	export MSYS2_ARG_CONV_EXCL=*; \
	${CLANG_CL} \
		/c $< \
		/Fo$@ \
		/nologo \
		${WASM_FLAGS} \
		${MSVC_INCLUDES} \
		${DEFINES} \
		-DWASM_API_DEBUG \
		-D_ITERATOR_DEBUG_LEVEL=0 \
		-I${WASM_INCLUDE} \
		-I${V8_V8} \
		-I${V8_OUT}/gen \
		-I${V8_V8}/include \
		-I${V8_V8}/src \
		-I${V8_V8}/third_party/abseil-cpp \
		-I${V8_OUT}/gen/include \
		-I${V8_V8}/third_party/fp16/src/include \
		-I${WASM_SRC} \
		${CFLAGS} \
		${CFLAGS_CC} \
		/Fd"$@.pdb" -v;

# wasm-c.cc includes wasm-v8.cc, so set up a side dependency
${WASM_OUT}/wasm-c.o: ${WASM_SRC}/wasm-v8.cc


###############################################################################
# Clean-up

.PHONY: clean
clean:
	rm -rf ${OUT_DIR}


###############################################################################
# V8
#
# To get and build V8:
#   make v8-checkout
#   make v8
#
# To update and build current branch:
#   make v8-update
#   make v8

# Building

.PHONY: v8
v8: ${V8_INCLUDE}/${WASM_V8_PATCH}.hh ${V8_SRC}/${WASM_V8_PATCH}.cc v8-patch v8-build v8-unpatch

.PHONY: v8-build
v8-build:
	@echo ==== Building V8 ${V8_CURRENT} ${V8_BUILD} ====
	(cd ${V8_V8}; PATH=${V8_PATH} ${PYTHON3} tools/dev/v8gen.py -vv ${V8_BUILD} -- ${V8_GN_ARGS})
	(cd ${V8_V8}; PATH=${V8_PATH} ninja -C out.gn/${V8_BUILD})
	(cd ${V8_V8}; touch out.gn/${V8_BUILD}/args.gn)
	(cd ${V8_V8}; PATH=${V8_PATH} ninja -C out.gn/${V8_BUILD})

WASM_V8_PATCH_FILE = $(abspath ${WASM_DIR}/patch/0001-BUILD.gn-add-wasm-v8-lowlevel.patch)
.PHONY: v8-patch
v8-patch:
	if ! grep ${WASM_V8_PATCH} ${V8_V8}/BUILD.gn; then \
	  cp ${V8_V8}/BUILD.gn ${V8_V8}/BUILD.gn.save; \
	  cd ${V8_V8}; \
	  patch < ${WASM_V8_PATCH_FILE}; \
	fi

.PHONY: v8-unpatch
v8-unpatch:
	if [ -f ${V8_V8}/BUILD.gn.save ]; then \
	  mv -f ${V8_V8}/BUILD.gn.save ${V8_V8}/BUILD.gn; \
	fi

${V8_INCLUDE}/${WASM_V8_PATCH}.hh: ${WASM_SRC}/${WASM_V8_PATCH}.hh
	cp $< $@

${V8_SRC}/${WASM_V8_PATCH}.cc: ${WASM_SRC}/${WASM_V8_PATCH}.cc
	cp $< $@


# Check-out

# Check out set version
.PHONY: v8-checkout
v8-checkout: v8-checkout-banner ${V8_DEPOT_TOOLS} ${V8_V8}
	(cd ${V8_V8}; git checkout -f master)
	(cd ${V8_V8}; git pull)
	(cd ${V8_V8}; git checkout ${V8_VERSION})
	(cd ${V8_V8}; PATH=${V8_PATH} gclient sync)
	mkdir -p ${V8_OUT}
	echo >${V8_OUT}/version ${V8_VERSION}
	@if [ ${V8_CURRENT} != ${V8_VERSION} ]; then echo ==== Done. If you have trouble building V8, run \`make v8-clean\` first ====; fi

.PHONY: v8-checkout-banner
v8-checkout-banner:
	@echo ==== Checking out V8 ${V8_VERSION} ====

${V8_DEPOT_TOOLS}:
	mkdir -p ${V8_DIR}
	(cd ${V8_DIR}; git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git)

${V8_V8}:
	mkdir -p ${V8_DIR}
	(cd ${V8_DIR}; PATH=${V8_PATH} fetch v8)
	(cd ${V8_V8}; git checkout ${V8_VERSION})

# Update current check-out
.PHONY: v8-update
v8-update: v8-unpatch
	@echo ==== Updating V8 ${V8_CURRENT} ====
	(cd ${V8_V8}; git pull origin ${V8_CURRENT})
	(cd ${V8_V8}; PATH=${V8_PATH} gclient sync)


# Clean-up

# Delete V8 build
.PHONY: v8-clean
v8-clean:
	rm -rf ${V8_OUT}
	mkdir -p ${V8_OUT}
	echo >${V8_OUT}/version ${V8_VERSION}


# Show current V8 version
.PHONY: v8-version
v8-version:
	@echo Checked out V8 version: ${V8_CURRENT}


# Display V8 build configuration
.PHONY: v8-gn-args
v8-gn-args:
	@echo ${V8_GN_ARGS}


###############################################################################
# Docker
#

.PHONY: docker
docker:
	docker build -t wasm:Dockerfile .
