// Copyright 2017 Hakan Metin - LIP6

#include "cosy/SymmetryFinder.h"
#include "cosy/BlissSymmetryFinder.h"
#include "cosy/SaucySymmetryFinder.h"

namespace cosy {

// static
SymmetryFinder*
SymmetryFinder::create(const CNFModel& model,
                       SymmetryFinder::Automorphism tool) {
    switch (tool) {
    case BLISS: return new BlissSymmetryFinder(model);
    case SAUCY: return new SaucySymmetryFinder(model);
    default: return nullptr;
    }
}

}  // namespace cosy
