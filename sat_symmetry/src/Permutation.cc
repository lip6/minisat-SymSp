// Copyright 2017 Hakan Metin - LIP6

#include "cosy/Permutation.h"

namespace cosy {

void Permutation::addToCurrentCycle(Literal x) {
    const int cs = _cycles.size();
    const int back = _cycles_lim.empty() ? 0 : _cycles_lim.back();
    _cycles.push_back(x);

    // Add image and inverse to access in lookup unordered_map
    if (_cycles.size() > 0 && cs != back) {
        const Literal e = _cycles[cs - 1];
        const Literal i = _cycles[cs];
        _image[e] = i;
        _inverse[i] = e;
    }
}

void Permutation::closeCurrentCycle() {
    const int sz = _cycles.size();
    int last = _cycles_lim.empty() ? 0 : _cycles_lim.back();

    if (last == sz)
        return;

    DCHECK_GE(sz - last, 2);
    _cycles_lim.push_back(sz);

    // Add image and inverse to access in lookup unordered_map
    const int num_cycle = _cycles_lim.size() - 1;
    const Literal e = lastElementInCycle(num_cycle);
    const Literal i = *(cycle(num_cycle).begin());
    _image[e] = i;
    _inverse[i] = e;
}

Permutation::Iterator Permutation::cycle(unsigned int i) const {
    //    DCHECK_GE(i, static_cast<unsigned int>(0));
    DCHECK_LT(i, numberOfCycles());

    return Iterator(_cycles.begin() + (i == 0 ? 0 : _cycles_lim[i - 1]),
        _cycles.begin() + _cycles_lim[i]);
}

Literal Permutation::lastElementInCycle(unsigned int i) const {
    // DCHECK_GE(i, static_cast<unsigned int>(0));
    DCHECK_LT(i, numberOfCycles());

    return _cycles[_cycles_lim[i] - 1];
}

const Literal Permutation::imageOf(const Literal& element) const {
    return _image.at(element);
}
const Literal Permutation::inverseOf(const Literal& element) const {
    return _inverse.at(element);
}

bool Permutation::isTrivialImage(const Literal& element) const {
    return _image.find(element) == _image.end();
}
bool Permutation::isTrivialInverse(const Literal& element) const {
    return _inverse.find(element) == _inverse.end();
}



static int gcd(int a, int b) {
    for (;;) {
        if (a == 0)
            return b;
        b %= a;
        if (b == 0)
            return a;
        a %= b;
    }
}
static int lcm(int a, int b) {
    int temp = gcd(a, b);

    return temp ? (a / temp * b) : 0;
}

int Permutation::order() const {
    int order = 1;

    for (unsigned int i=0; i<numberOfCycles(); i++) {
        order = lcm(order, cycle(i).size());
    }

    return order;
}


std::unique_ptr<Permutation> Permutation::mult(int order) const {
    std::unique_ptr<Permutation> perm(new Permutation(_size));

    std::unordered_map<Literal, Literal> image;
    std::vector<bool> seen(_size << 2, false);

    for (Literal l : support()) {
        Literal i = l;
        for (int k=0; k<order; k++)
            if (isTrivialImage(i))
                break;
            else
                i = imageOf(i);
        image[l] = i;
    }

    for (auto &pair : image) {
        Literal l = pair.first;

        if (seen[l.index().value()])
            continue;
        seen[l.index().value()] = true;
        perm->addToCurrentCycle(l);
        Literal i = image[l];
        while (i != l) {
            perm->addToCurrentCycle(i);
            seen[i.index().value()] = true;
            i = image[i];
        }
        perm->closeCurrentCycle();
    }

    return std::move(perm);
}

void Permutation::debugPrint() const {
    CHECK_NE(numberOfCycles(), 0);

    for (unsigned int c = 0; c < numberOfCycles(); ++c) {
        std::cout << "(";
        for (const Literal& element : cycle(c)) {
            if (element == lastElementInCycle(c))
                std::cout << element.signedValue();
            else
                std::cout << element.signedValue() << " ";
        }
        std::cout << ")";
    }
    std::cout << std::endl;
}


}  // namespace cosy


/*
 * Local Variables:
 * mode: c++
 * indent-tabs-mode: nil
 * End:
 */
