#ifndef OPENCMW_CPP_MULTI_ARRAY_HPP
#define OPENCMW_CPP_MULTI_ARRAY_HPP

#include <array>
#include <fmt/color.h>
#include <fmt/format.h>
#include <iostream>
#include <vector>

namespace opencmw {

template<typename T>
concept AnyNumber = std::is_arithmetic_v<T>;

/// MultiArray implementation with a fixed number of dimensions but variable number of elements
/// \tparam T type of the data entries
/// \tparam n_dims number of dimensions >= 1 (1 -> vector, 2-> matrix, etc)
template<typename T, uint32_t n_dims>
class MultiArray {
    static_assert(n_dims >= 1, "n_dims must be larger than 0");

public:
    using size_t_                    = uint32_t; // alternative: using size_t_ = size_t; // have to use signed int here?, because that's what is used in the serialised protocol (bc of java)
    using value_type                 = T;
    static constexpr size_t_ n_dims_ = n_dims;

private:
    std::vector<value_type>     elements_;  // data
    size_t_                     n_element_; // number of valid entries in data
    std::array<size_t_, n_dims> dims_;      // sizes for every dimension
    std::array<size_t_, n_dims> strides_;   // strides_ for every dimension
    std::array<size_t_, n_dims> offsets_;   // offset for every dimension
public:
    /// full control constructor, allowing to realise custom matrix layouts
    [[nodiscard]] constexpr MultiArray(
            const std::vector<value_type>      elements,
            const std::array<size_t_, n_dims> &dimensions,
            const std::array<size_t_, n_dims> &strides,
            const std::array<size_t_, n_dims>  offsets)
        : elements_(elements), n_element_(size_t_(elements.size())), dims_(dimensions), strides_(strides), offsets_(offsets) {}

    /// copy constructor for wrapping a linear vector of elements
    /// \param elements the elements in row major format
    /// \param dimensions the size of the MultiArray in every dimension
    [[nodiscard]] constexpr MultiArray(const std::vector<value_type> elements, const std::array<size_t_, n_dims> &dimensions)
        : elements_(elements.data(), elements.size()), n_element_(dimensions[n_dims - 1]), dims_(dimensions), strides_(), offsets_() {
        strides_[n_dims - 1] = 1;
        for (auto i = n_dims - 1; i > 0; i--) {
            n_element_ *= dimensions[i - 1];
            strides_[i - 1] = strides_[i] * dimensions[i];
        }
    }

    /// move constructor
    /// \param elements the elements in row major format
    /// \param dimensions the size of the MultiArray in every dimension
    [[nodiscard]] constexpr MultiArray(const std::vector<value_type> &&elements, const std::array<size_t_, n_dims> &&dimensions)
        : elements_(std::move(elements)), n_element_(dimensions[n_dims - 1]), dims_(std::move(dimensions)), strides_(), offsets_() {
        strides_[n_dims - 1] = 1;
        for (auto i = n_dims - 1; i > 0; i--) {
            n_element_ *= dimensions[i - 1];
            strides_[i - 1] = strides_[i] * dimensions[i];
        }
    }

    /// constructor for an empty MultiArray of given size
    /// \param dimensions the size of the MultiArray in every dimension
    [[nodiscard]] constexpr explicit MultiArray(const std::array<size_t_, n_dims> &dimensions = std::array<size_t_, n_dims>())
        : n_element_(dimensions[n_dims - 1]), dims_(dimensions), strides_(), offsets_() {
        strides_[n_dims - 1] = 1;
        for (auto i = n_dims - 1; i > 0; i--) {
            n_element_ *= dimensions[static_cast<unsigned long>(i - 1)];
            strides_[i - 1] = strides_[i] * dimensions[i];
        }
        elements_ = std::vector<value_type>(n_element_);
    }

    void clear() {
        // elements_.clear();
        // n_element_ = 0;
        // std::iota(dims_.begin(), dims_.end(),0);
        // std::iota(strides_.begin(), strides_.end(),0);
        // std::iota(offsets_.begin(), offsets_.end(),0);
    }

    /// \return the number of dimensions of the MultiArray
    [[nodiscard]] constexpr const auto &dimensions() const noexcept {
        return dims_;
    }

    /// \return the mutable numbers of dimensions of the MultiArray
    [[nodiscard]] constexpr auto &&dimensions() noexcept {
        return dims_;
    }

    /// \return the raw vector with the elements of the MultiArray
    [[nodiscard]] constexpr const auto &elements() const noexcept {
        return elements_;
    }

    /// \return the mutable raw vector with the elements of the MultiArray
    [[nodiscard]] constexpr auto &&elements() noexcept {
        return elements_;
    }

    /// \return the number of valid entries in the raw vector
    [[nodiscard]] constexpr auto &element_count() noexcept {
        return n_element_;
    }
    [[nodiscard]] constexpr auto &element_count() const noexcept {
        return n_element_;
    }

    /// \param dim the dimension to return the offset for
    /// \return the offset of the data in the raw array for the given dimension
    [[nodiscard]] constexpr auto &offset(size_t_ dim) noexcept {
        return offsets_[dim];
    }
    [[nodiscard]] constexpr auto &offset(size_t_ dim) const noexcept {
        return offsets_[dim];
    }

    /// \param dim the dimension to return the stride for
    /// \return the stride of the data in the raw array for the given dimension
    [[nodiscard]] constexpr auto &stride(size_t_ dim) noexcept {
        return strides_[dim];
    }
    [[nodiscard]] constexpr auto &stride(size_t_ dim) const noexcept {
        return strides_[dim];
    }

    /// \param dim the dimension to return the size for
    /// \return the number of rows/columns/slices/... of the data in the raw array for the given dimension
    [[nodiscard]] constexpr const auto &n(size_t_ dim) const noexcept {
        return dims_[dim];
    }

    /// compute the scalar index of an entry in the MultiArray
    /// \param indices indices for every dimension
    /// \return a scalar index in the multi array
    [[nodiscard]] constexpr size_t_ index(const std::array<size_t_, n_dims> indices) const noexcept {
        size_t_ index = 0;
        for (size_t_ i = 0; i < n_dims; i++) {
            index += (offsets_[i] + indices[i]) * strides_[i];
        }
        return index;
    }

    /// compute the multi-index from the scalar index in the backing data
    /// \param index scalar index, has to be a position with valid data
    /// \return corresponding multi index
    [[nodiscard]] constexpr std::array<size_t_, n_dims> indices(const size_t_ index) const noexcept {
        std::array<size_t_, n_dims> indices{};
        size_t_                     index_ = index;
        for (size_t_ i = 0; i < n_dims; i++) {
            index_ -= offsets_[i];
            indices[i] = index_ / strides_[i];
            index_ -= indices[i] * strides_[i];
        }
        return indices;
    }

    /// access operator for linear access
    [[nodiscard]] constexpr value_type &operator[](const size_t_ index) {
        return elements_[index];
    }

    /// access operator for multiple dimensions
    [[nodiscard]] constexpr value_type &operator[](const std::array<size_t_, n_dims> indices) {
        return elements_[index(indices)];
    }

    /// access operator for linear access (parentheses version)
    [[nodiscard]] constexpr value_type &operator()(const size_t_ index) {
        return elements_[index];
    }

    /// allow access with parenthesis operator (no curly braces)
    template<typename... R, typename std::enable_if<sizeof...(R) == n_dims && (true && ... && std::convertible_to<R, size_t_>), size_t_>::type = 0>
    [[nodiscard]] constexpr value_type &operator()(const R... indices) {
        return elements_[index(std::array{ indices... })];
    }

    /// explicit method linear accessor (bounds checked)
    [[nodiscard]] constexpr value_type &get(const size_t_ index) {
        return elements_.at(index);
    }

    /// explicit method multi array accessor (bounds checked)
    [[nodiscard]] constexpr value_type &get(const std::array<size_t_, n_dims> indices) {
        return elements_.at(index(indices));
    }
    template<typename... R, typename std::enable_if<sizeof...(R) == n_dims && (true && ... && std::convertible_to<R, size_t_>), size_t_>::type = 0>
    [[nodiscard]] constexpr value_type &get(const R... indices) {
        return elements_.at(index(std::array{ indices... }));
    }

    // math operator overloads [+-*/[=]] arithmetic<value_type> MultiArray<value_type> vector<value_type> (both lvalue and rvalue version)
    MultiArray &operator+=(const MultiArray<value_type, n_dims> operand) {
        // todo: verify dimension match. Allow broadcasting (adding a column vector to each row or similar)?
        for (size_t_ i = 0; i < n_element_; i++) { // todo: use iterator to only change valid fields
            this[i] += operand[i];
        }
        return *this;
    }
    template<AnyNumber R>
    MultiArray &operator+=(const R operand) {
        for (size_t_ i = 0; i < n_element_; i++) { // todo: use iterator to only change valid fields
            this[i] += operand;
        }
        return *this;
    }

    MultiArray<value_type, n_dims> operator+(const MultiArray<value_type, n_dims> operand) {
        // todo: verify dimension match. Allow broadcasting (adding a column vector to each row or similar)?
        MultiArray &result = MultiArray<value_type, n_dims>(this->dims_);
        for (size_t_ i = 0; i < n_element_; i++) { // todo: use iterator to only change valid fields
            result[i] = this[i] + operand[i];
        }
        return result;
    }
    template<AnyNumber R>
    MultiArray &operator+(const R operand) {
        MultiArray &result = MultiArray<value_type, n_dims>(this->dims_);
        for (size_t_ i = 0; i < n_element_; i++) { // todo: use iterator to only change valid fields
            this[i] += operand;
        }
        return result;
    }

    bool operator==(const MultiArray<value_type, n_dims> &other) const noexcept {
        // check if array is the same size
        for (size_t_ i = 0; i < n_dims; i++) {
            if (dims_[i] != other.dims_[i]) {
                return false;
            }
        }
        // check content of array todo: respect offsets/strides/etc
        for (size_t_ i = 0; i < n_element_; i++) {
            if (elements_[i] != other.elements_[i]) {
                return false;
            }
        }
        return true;
    }

    // lambda function on a per cell operation

    // iterators todo: research possibility to have multidimensional iterators. nested iterators?
    // array.iterator(1[,0])->iterator(1[,2]) // column iterator starting at the third entry of the first row
    // forward/backward/const iterator -> enables ranges, <algorithm>, and <numeric>

    // sub-arrays / slices / compact array
    /// extract a row/column from an array
    // constexpr MultiArrayView<value_type, n_dims - 1> slice(const size_t_ nDim, const size_t_ index) {
    //     auto new_dims = std::array<size_t_, n_dims - 1>;
    //     auto new_offsets = std::array<size_t_, n_dims - 1>;
    //     for (size_t_ i = 0; i < n_dims -1; i++) {
    //         if (i < nDim) {
    //             new_dims[i] = dims_[i];
    //         } else {
    //             new_dims[i] = dims_[i+1];
    //         }
    //     }

    //     return MultiArray(elements(), new_dims, new_offsets);
    //     // return MultiArray(elements_, ); // todo: implement logic
    // }
    /// extract a sub-matrix
    //constexpr MultiArrayView<value_type, n_dims> sub_array(const std::array<size_t, n_dims> min_indices, const std::array<size_t, n_dims> max_indices) {
    //    return nullptr;
    //}
    /// sample down a matrix by skipping every nth value in every dimension
    //constexpr MultiArrayView<value_type, n_dims> downsample(const std::array<size_t, n_dims> strides_) {
    //    return nullptr;
    //}

    /// simple print operator for debugging
    constexpr friend std::ostream &operator<<(std::ostream &output, const MultiArray &array) {
        //return output << fmt::format("{{dim[{}]:{}, data[{}]:{}}}", n_dims, array.dimensions(), array.element_count(), array.elements()); // does not work
        return output << fmt::format("{{dim[{}]:{}, data[{}]:data display not implemented}}", n_dims, array.dimensions(), array.element_count()); // does not work
    }
};

// MultiArrayView -> uses data from Multi Array, but with different layout/number of dimensions -> can represent slices and sampled down views of the data
// multiArray          // 3d array
//    .slice(1,3)      // get 3rd column
//    .subArray(0,5,7) // get 5. to 7. row
//    .downsample(2,2) // only use every other sample in z direction
// views can be assigned to and this will affect the original array, so you can use this to blank out/initialize specific matrix elements
// materialise a view into its own MultiArray by using the MultiArray(MultiArrayView) constructor

// MultiArrayIterator -> returns iterator of T
// MultiArrayDimIterator -> returns iterator of MultiArrayView<T, dim-1>
// iterators should be aware if they represent congruent memory to allow using memmove type copying if applicable

template<typename T>
inline constexpr bool is_multi_array = false;

template<typename T, uint32_t n_dims>
inline constexpr bool is_multi_array<MultiArray<T, n_dims>> = true;

template<typename T>
concept MultiArrayType = is_multi_array<T>;

} // namespace opencmw

#endif //OPENCMW_CPP_MULTI_ARRAY_HPP