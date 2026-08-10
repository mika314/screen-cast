all: FORCE coddle/coddle
	coddle/coddle

coddle/coddle:
	$(MAKE) -j$$(nproc) -C coddle

clean: FORCE
	rm -rf .coddle
	$(MAKE) clean -j$$(nproc) -C coddle

FORCE:
