# LibTorch-backed tensor operations over numeric arrays. Non-scalar results
# remain tensors until the VM display boundary.

main() =>
    return (
        dot: ml.dot(left: [1, 2, 3], right: [4, 5, 6]),
        mse: ml.meanSquaredError(left: [1, 2], right: [1, 4]),
        sigmoid: ml.sigmoid(value: [-1, 0, 1]),
        relu: ml.relu(value: [-2, 0, 3]),
        size: tensor.size(value: [[1, 2], [3, 4]]),
        shape: tensor.shape(value: [[1, 2], [3, 4]]),
        dimensions: tensor.dimensions(value: [[1, 2], [3, 4]]),
        difference: tensor.difference(left: [1, 4], right: [3, 1]),
        cosine_similarity: tensor.cosineSimilarity(left: [1, 0], right: [1, 1]),
        transpose: tensor.transpose(value: [[1, 2], [3, 4]]),
        symmetric: tensor.isSymmetric(value: [[1, 2], [2, 1]]),
        copy: tensor.clone(value: [[1, 2], [3, 4]])
    )
