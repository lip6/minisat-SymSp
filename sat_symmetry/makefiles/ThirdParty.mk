
third_party: bliss

bliss:
	$(MAKE) -C third_party/automorphism/bliss/ lib

clean-third_party:
	$(MAKE) -C third_party/automorphism/bliss/ clean

.PHONY: third_party clean-third_party
