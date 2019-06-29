#!/bin/bash
structs=($(rg 'struct [a-z_]+_t' \
	| grep struct \
	| awk -F : '{ print $2 }' \
	| awk -F ' ' '{ print $2 }' \
	| grep -v -e '<' -e '>' \
	| sed -Ee 's/;//'))

for s in ${structs[*]}; do
	echo $s $(./camelize $s)
	sed -i .bak "s/[[:<:]]$s[[:>:]]/$(./camelize "$s")/g" ./**/*.cpp ./**/*.h
	rm ./**/*.{cpp,h}.bak
done

