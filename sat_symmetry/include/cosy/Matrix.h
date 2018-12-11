// Copyright 2017 Hakan Metin - LIP6

#ifndef INCLUDE_COSY_MATRIX_H_
#define INCLUDE_COSY_MATRIX_H_

namespace cosy {

class Matrix {
 public:
    Matrix();
    ~Matrix();

    bool canAugmentGenerator(const std::unique_ptr<Permutation> & perm);

 private:
    std::vector< std::vector<Literal> > _matrix;
};

}  // namespace cosy

#endif // INCLUDE_COSY_MATRIX_H_

/*
 * Local Variables:
 * mode: c++
 * indent-tabs-mode: nil
 * End:
 */
