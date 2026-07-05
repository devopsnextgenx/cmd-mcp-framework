#!/bin/bash

set -u

PATCH_ROOT="patch"
SRC_ROOT="build/_deps"

if [[ ! -d "$PATCH_ROOT" ]]; then
	echo "Error: patch directory not found: $PATCH_ROOT" >&2
	exit 1
fi

generated=0
skipped=0
errors=0

while IFS= read -r -d '' patched_file; do
	rel_path="${patched_file#${PATCH_ROOT}/}"
	src_file="${SRC_ROOT}/${rel_path}"
	patch_file="${patched_file}.patch"

	if [[ ! -f "$src_file" ]]; then
		echo "Skip (source missing): $src_file"
		((skipped++))
		continue
	fi

	# Write the unified diff to a sibling .patch file.
	if diff -u "$src_file" "$patched_file" > "$patch_file"; then
		:
	else
		diff_status=$?
		if [[ $diff_status -ne 1 ]]; then
			echo "Error generating patch for: $patched_file" >&2
			((errors++))
			continue
		fi
	fi

	echo "Generated: $patch_file"
	((generated++))
done < <(
	find "$PATCH_ROOT" -type f \
		\( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.cxx" -o -name "*.hxx" \) \
		-print0
)

echo "Done. generated=$generated skipped=$skipped errors=$errors"

if [[ $errors -gt 0 ]]; then
	exit 1
fi

exit 0

