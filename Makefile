# SPDX-License-Identifier: BSD-2-Clause
.PHONY: check clean

all: fork

check: fork
	@cd t && $(MAKE)

clean:
	@cd t && $(MAKE) clean
	rm -f fork *.o
