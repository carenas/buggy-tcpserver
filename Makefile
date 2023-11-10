# SPDX-License-Identifier: BSD-2-Clause
.PHONY: check clean

all: fork

check: fork
	@cd t && $(MAKE)

clean:
	@cd t && make clean
	rm -f fork *.o
