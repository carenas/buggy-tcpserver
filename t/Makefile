# SPDX-License-Identifier: BSD-2-Clause
.PHONY: all check clean

check: ../fork
	./regress

all: check

clean:
	rm -f err exp log out
