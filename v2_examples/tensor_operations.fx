# LibTorch-backed tensor operations over numeric arrays. Fact and text values
# use a real tensor feature view containing SentencePiece IDs and typed numeric
# fields. Non-scalar results remain tensors until the VM display boundary.

Observation(sensor: "", state: "", value: 0)

main() =>
    reading := Observation(sensor: "indoor", state: "steady", value: 21.5)
    changed_reading := Observation(sensor: "indoor", state: "steady", value: 24.0)
    reading_tensor := tensor.clone(value: reading)
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
        copy: tensor.clone(value: [[1, 2], [3, 4]]),
        fact_tensor: reading_tensor,
        fact_size: tensor.size(value: reading),
        fact_shape: tensor.shape(value: reading),
        fact_self_similarity: tensor.cosineSimilarity(
            left: reading,
            right: reading_tensor
        ),
        fact_self_difference: tensor.difference(
            left: reading,
            right: reading_tensor
        ),
        fact_value_difference: tensor.difference(
            left: reading,
            right: changed_reading
        ),
        field_similarity: tensor.cosineSimilarity(
            left: reading.sensor,
            right: changed_reading.sensor
        ),
        field_difference: tensor.difference(
            left: reading.value,
            right: changed_reading.value
        )
    )
