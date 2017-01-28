#!/bin/sh

#Convert paths from Windows style to POSIX style
MSYS_BIN=$(echo "/$MSYS_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')

# Set PATH using POSIX style paths
PATH="$MSYS_BIN:$MINGW_BIN:$PATH"

# Ensure git has "executable link" installed
GIT_PATH=/bin/git
if [ -n "$GIT_EXE" ]
then
	GIT_EXE=$(echo "/$GIT_EXE" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
	echo Installng git using git.exe direct path
	echo "#!/bin/sh" > "$GIT_PATH"
	echo 'exec "'"$GIT_EXE"'" "$@"' >> "$GIT_PATH"
	chmod +x "$GIT_PATH"
fi


# Ensure python, python2, and python3 have "executable links" installed
PYTHON_PATH=/bin/python
PYTHON2_PATH=/bin/python2
PYTHON3_PATH=/bin/python3
# If the python version selector is installed (py.exe), use it for all paths
if [ -n "$PY_EXE" ]
then
	PY_EXE=$(echo "/$PY_EXE" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
	echo Installng python, python2, and python3 using py.exe \(python version selector\)
	# Ensure python has "executable link" installed
	echo "#!/bin/sh" > "$PYTHON_PATH"
	echo "# Allow python version selector to choose which version to run" >> "$PYTHON_PATH"
	echo 'exec "'"$PY_EXE"'" "$@"' >> "$PYTHON_PATH"
	chmod +x "$PYTHON_PATH"

	# Ensure python2 has "executable link" installed
	echo "#!/bin/sh" > "$PYTHON2_PATH"
	echo "# Force use of python version 2.x" >> "$PYTHON2_PATH"
	echo 'exec "'"$PY_EXE"'" -2 "$@"' >> "$PYTHON2_PATH"
	chmod +x "$PYTHON2_PATH"

	# Ensure python3 has "executable link" installed
	echo "#!/bin/sh" > "$PYTHON3_PATH"
	echo "# Force use of python version 3.x" >> "$PYTHON3_PATH"
	echo 'exec "'"$PY_EXE"'" -3 "$@"' >> "$PYTHON3_PATH"
	chmod +x "$PYTHON3_PATH"
else
	# If the python3 path is specified (python.exe), use it for python3 and python
	if [ -n "$PYTHON3_EXE" ]
	then
		PYTHON3_EXE=$(echo "/$PYTHON3_EXE" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
		echo Installng python3 using python3 direct path
		# Install python3
		echo "#!/bin/sh" > "$PYTHON3_PATH"
		echo "# Use direct reference to installed version of python 3.x" >> "$PYTHON3_PATH"
		echo 'exec "'"$PYTHON3_EXE"'" "$@"' >> "$PYTHON3_PATH"
		chmod +x "$PYTHON3_PATH"
		
		echo Installng python using python3 direct path
		# Also install python to use python3
		echo "#!/bin/sh" > "$PYTHON_PATH"
		echo "# Use direct reference to installed version of python 3.x" >> "$PYTHON_PATH"
		echo 'exec "'"$PYTHON3_EXE"'" "$@"' >> "$PYTHON_PATH"
		chmod +x "$PYTHON_PATH"
	fi
	
	# If the python2 path is specified (python.exe), use it for python2
	if [ -n "$PYTHON2_EXE" ]
	then
		PYTHON2_EXE=$(echo "/$PYTHON2_EXE" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
		echo Installng python2 using python2 direct path
		# Install python2
		echo "#!/bin/sh" > "$PYTHON2_PATH"
		echo "# Use direct reference to installed version of python 2.x" >> "$PYTHON2_PATH"
		echo 'exec "'"$PYTHON2_EXE"'" "$@"' >> "$PYTHON2_PATH"
		chmod +x "$PYTHON2_PATH"
		
		# Also install python to use python2, but ONLY IF python3 path not specified
		if [ -z "$PYTHON3_EXE" ]
		then
			echo Installng python using python2 direct path
			echo "#!/bin/sh" > "$PYTHON_PATH"
			echo "# Use direct reference to installed version of python 2.x" >> "$PYTHON_PATH"
			echo 'exec "'"$PYTHON2_EXE"'" "$@"' >> "$PYTHON_PATH"
			chmod +x "$PYTHON_PATH"
		fi
	fi
fi
