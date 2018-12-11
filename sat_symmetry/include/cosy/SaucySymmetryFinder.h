// Copyright 2017 Hakan Metin - LIP6

#ifndef INCLUDE_COSY_SAUCYSYMMETRYFINDER_H_
#define INCLUDE_COSY_SAUCYSYMMETRYFINDER_H_

#include <string>

#include "cosy/Macros.h"
#include "cosy/SymmetryFinder.h"
#include "cosy/CNFGraph.h"
#include "cosy/Group.h"

namespace cosy {

class SaucySymmetryFinder : public SymmetryFinder {
 public:
    explicit SaucySymmetryFinder(const CNFModel& model) :
        SymmetryFinder(model) {}
    ~SaucySymmetryFinder() {}

    void findAutomorphism(Group *group) override;
    std::string toolName() const override { return std::string("Saucy3"); }
};

}  // namespace cosy

#endif  // INCLUDE_COSY_SAUCYSYMMETRYFINDER_H_
/*
 * Local Variables:
 * mode: c++
 * indent-tabs-mode: nil
 * End:
 */
