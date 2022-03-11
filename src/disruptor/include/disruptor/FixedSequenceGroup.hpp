#pragma once

#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "ISequence.hpp"
#include "Util.hpp"

namespace opencmw::disruptor {

/**
 * Hides a group of Sequences behind a single Sequence
 */
class FixedSequenceGroup : public ISequence {
private:
    std::vector<std::shared_ptr<ISequence>> _sequences;

public:
    /**
     *
     * \param sequences sequences the list of sequences to be tracked under this sequence group
     */
    explicit FixedSequenceGroup(std::vector<std::shared_ptr<ISequence>> sequences)
        : _sequences(std::move(sequences)) {
    }

    /**
     * Get the minimum sequence value for the group.
     */
    [[nodiscard]] std::int64_t value() const noexcept override {
        return util::getMinimumSequence(_sequences);
    }

    /**
     * Not supported.
     */
    void setValue(std::int64_t /*value*/) override {
        throw std::logic_error("unsupported operation: FixedSequenceGroup::setValue");
    }

    /**
     * Not supported.
     */
    bool compareAndSet(std::int64_t /*expectedValue*/, std::int64_t /*newValue*/) override {
        throw std::logic_error("unsupported operation: FixedSequenceGroup::compareAndSet");
    }

    /**
     * Not supported.
     */
    std::int64_t incrementAndGet() override {
        throw std::logic_error("unsupported operation: FixedSequenceGroup::incrementAndGet");
    }

    /**
     * Not supported.
     */
    std::int64_t addAndGet(std::int64_t /*increment*/) override {
        throw std::logic_error("unsupported operation: FixedSequenceGroup::addAndGet");
    }

    void writeDescriptionTo(std::ostream &stream) const override {
        auto firstItem = true;
        for (auto &&sequence : _sequences) {
            if (firstItem) {
                firstItem = false;
            } else {
                stream << ", ";
            }
            stream << "{ ";
            sequence->writeDescriptionTo(stream);
            stream << " }";
        }
    }
};

} // namespace opencmw::disruptor
