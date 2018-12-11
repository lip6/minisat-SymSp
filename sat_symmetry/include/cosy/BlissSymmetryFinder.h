// Copyright 2017 Hakan Metin - LIP6

#ifndef INCLUDE_COSY_BLISSSYMMETRYFINDER_H_
#define INCLUDE_COSY_BLISSSYMMETRYFINDER_H_

#include <string>

#include "cosy/SymmetryFinder.h"
#include "cosy/CNFGraph.h"
#include "cosy/Group.h"

namespace cosy {

class BlissSymmetryFinder : public SymmetryFinder {
 public:
    explicit BlissSymmetryFinder(const CNFModel& model) :
        SymmetryFinder(model) {}
    ~BlissSymmetryFinder() {}

    void findAutomorphism(Group *group) override;
    std::string toolName() const override { return std::string("Bliss"); }
};

}  // namespace cosy

#endif  // INCLUDE_COSY_BLISSSYMMETRYFINDER_H_
/*
 * Local Variables:
 * mode: c++
 * indent-tabs-mode: nil
 * End:
 */
