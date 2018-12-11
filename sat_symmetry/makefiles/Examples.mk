
EXAMPLES := examples/

examples: default $(BIN)CNFBlissSymmetries $(BIN)CNFSaucySymmetries
# solvers: $(BIN)minisat
solvers: $(BIN)minisat-release
# solvers: $(BIN)minisat-simp
solvers: $(BIN)minisat-simp-release

$(call REQUIRE-DIR, $(BIN)CNFBlissSymmetries)
$(call REQUIRE-DIR, $(BIN)CNFSaucySymmetries)
$(call REQUIRE-DIR, $(BIN)minisat)
$(call REQUIRE-DIR, $(BIN)minisat-release)
$(call REQUIRE-DIR, $(BIN)minisat-simp)
$(call REQUIRE-DIR, $(BIN)minisat-simp-release)

$(BIN)CNFBlissSymmetries: LDFLAGS += -lcosy -lbliss  -lz
$(BIN)CNFBlissSymmetries: $(EXAMPLES)CNFBlissSymmetries.cc
	$(call cmd-cxx-bin, $@, $<, $(LDFLAGS))

$(BIN)CNFSaucySymmetries: LDFLAGS += -lcosy -lsaucy  -lz
$(BIN)CNFSaucySymmetries: $(EXAMPLES)CNFSaucySymmetries.cc
	$(call cmd-cxx-bin, $@, $<, $(LDFLAGS))


##### Solvers
# Minisat

FORCE:

$(BIN)minisat: default FORCE
	$(call cmd-make, clean, $(EXAMPLES)solvers/minisat/core)
	$(call cmd-make, , $(EXAMPLES)solvers/minisat/core)
	$(call cmd-cp, $@, $(EXAMPLES)solvers/minisat/core/minisat)

$(BIN)minisat-release: default FORCE
	$(call cmd-make, clean, $(EXAMPLES)solvers/minisat/core)
	$(call cmd-make, r, $(EXAMPLES)solvers/minisat/core)
	$(call cmd-cp, $@, $(EXAMPLES)solvers/minisat/core/minisat_release)

$(BIN)minisat-simp: default FORCE
	$(call cmd-make, clean, $(EXAMPLES)solvers/minisat/simp)
	$(call cmd-make, , $(EXAMPLES)solvers/minisat/simp)
	$(call cmd-cp, $@, $(EXAMPLES)solvers/minisat/simp/minisat)

$(BIN)minisat-simp-release: default FORCE
	$(call cmd-make, clean, $(EXAMPLES)solvers/minisat/simp)
	$(call cmd-make, r, $(EXAMPLES)solvers/minisat/simp)
	$(call cmd-cp, $@, $(EXAMPLES)solvers/minisat/simp/minisat_release)

clean-solvers:
	$(call cmd-make, clean, $(EXAMPLES)solvers/minisat/core)


.PHONY: examples FORCE
