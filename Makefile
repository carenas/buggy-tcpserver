# SPDX-License-Identifier: BSD-2-Clause
.PHONY: check clean

all: fork

check: fork
	$(MAKE) -C t

clean:
	$(RM) fork *.o
