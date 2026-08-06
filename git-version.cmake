set(GIT_VERSION_FILE "${OUTPUT_DIR}/git-version.cpp")
set(GIT_VERSION "unknown")
set(GIT_VERSION_UPDATE "1")

# `execute_process` with the MSYS git hangs on Windows (child-process wait
# bug), which stalls every CMake reconfigure.  The git hash is cosmetic for
# the Switch stub; read it from an env var only (set by CI), else "unknown".
if(DEFINED ENV{PPSSPP_GIT_VERSION})
	set(GIT_VERSION "$ENV{PPSSPP_GIT_VERSION}")
endif()

if(EXISTS ${GIT_VERSION_FILE})
	# Don't update if marked not to update.
	file(STRINGS ${GIT_VERSION_FILE} match
		REGEX "PPSSPP_GIT_VERSION_NO_UPDATE 1")
	if(NOT ${match} EQUAL "")
		set(GIT_VERSION_UPDATE "0")
	endif()

	# Let's also skip if it's the same.
	string(REPLACE "." "\\." GIT_VERSION_ESCAPED ${GIT_VERSION})
	file(STRINGS ${GIT_VERSION_FILE} match
		REGEX "PPSSPP_GIT_VERSION = \"${GIT_VERSION_ESCAPED}\";")
	if(NOT ${match} EQUAL "")
		set(GIT_VERSION_UPDATE "0")
	endif()
endif()

set(code_string "// This is a generated file.\n\n"
	"const char *PPSSPP_GIT_VERSION = \"${GIT_VERSION}\"\;\n\n"
	"// If you don't want this file to update/recompile, change to 1.\n"
	"#define PPSSPP_GIT_VERSION_NO_UPDATE 0\n")

if ("${GIT_VERSION_UPDATE}" EQUAL "1")
	file(WRITE ${GIT_VERSION_FILE} ${code_string})
endif()
