#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ICursored.hpp"
#include "ISequence.hpp"

namespace opencmw::disruptor {

class ICursored;
class ISequence;

/**
 * Provides static methods for managing a SequenceGroup object
 */
class SequenceGroups {
public:
    static void addSequences(std::shared_ptr<std::vector<std::shared_ptr<ISequence>>> &sequences, const ICursored &cursor, const std::vector<std::shared_ptr<ISequence>> &sequencesToAdd) {
        std::int64_t                                             cursorSequence;
        std::shared_ptr<std::vector<std::shared_ptr<ISequence>>> updatedSequences;
        std::shared_ptr<std::vector<std::shared_ptr<ISequence>>> currentSequences;

        do {
            currentSequences = std::atomic_load_explicit(&sequences, std::memory_order_acquire);

            updatedSequences = std::make_shared<std::vector<std::shared_ptr<ISequence>>>(currentSequences->size() + sequencesToAdd.size());

            std::ranges::copy(currentSequences->begin(), currentSequences->end(), updatedSequences->begin());

            cursorSequence = cursor.cursor();

            auto index     = currentSequences->size();
            for (auto &&sequence : sequencesToAdd) {
                sequence->setValue(cursorSequence);
                (*updatedSequences)[index++] = sequence;
            }
        } while (!std::atomic_compare_exchange_weak(&sequences, &currentSequences, updatedSequences)); // xTODO: explicit memory order

        cursorSequence = cursor.cursor();

        for (auto &&sequence : sequencesToAdd) {
            sequence->setValue(cursorSequence);
        }
    }

    static void addSequences(std::vector<std::shared_ptr<ISequence>> &sequences, const ICursored &cursor, const std::vector<std::shared_ptr<ISequence>> &sequencesToAdd) {
        std::int64_t cursorSequence;

        auto         updatedSize = sequences.size() + sequencesToAdd.size();

        cursorSequence           = cursor.cursor();

        auto index               = sequences.size();
        sequences.resize(updatedSize);

        for (auto &&sequence : sequencesToAdd) {
            sequence->setValue(cursorSequence);
            sequences[index++] = sequence;
        }

        cursorSequence = cursor.cursor();

        for (auto &sequence : sequencesToAdd) {
            sequence->setValue(cursorSequence);
        }
    }

    static bool removeSequence(std::shared_ptr<std::vector<std::shared_ptr<ISequence>>> &sequences, const std::shared_ptr<ISequence> &sequence) {
        std::uint32_t                                            numToRemove;
        std::shared_ptr<std::vector<std::shared_ptr<ISequence>>> oldSequences;
        std::shared_ptr<std::vector<std::shared_ptr<ISequence>>> newSequences;

        do {
            oldSequences = std::atomic_load_explicit(&sequences, std::memory_order_acquire);

            numToRemove  = countMatching(*oldSequences, sequence);

            if (numToRemove == 0)
                break;

            auto oldSize = static_cast<std::uint32_t>(oldSequences->size());
            newSequences = std::make_shared<std::vector<std::shared_ptr<ISequence>>>(oldSize - numToRemove);

            for (auto i = 0U, pos = 0U; i < oldSize; ++i) {
                auto &&testSequence = (*oldSequences)[i];
                if (sequence != testSequence) {
                    (*newSequences)[pos++] = testSequence;
                }
            }
        } while (!std::atomic_compare_exchange_weak(&sequences, &oldSequences, newSequences));

        return numToRemove != 0;
    }

    static bool removeSequence(std::vector<std::shared_ptr<ISequence>> &sequences, const std::shared_ptr<ISequence> &sequence) {
        std::uint32_t numToRemove = countMatching(sequences, sequence);
        if (numToRemove == 0)
            return false;

        auto                                    oldSize = static_cast<std::uint32_t>(sequences.size());
        std::vector<std::shared_ptr<ISequence>> newVector(oldSize - numToRemove);

        for (auto i = 0U, pos = 0U; i < oldSize; ++i) {
            auto &&testSequence = sequences[i];
            if (sequence != testSequence) {
                newVector[pos++] = testSequence;
            }
        }
        std::swap(sequences, newVector);

        return numToRemove != 0;
    }

private:
    static std::uint32_t countMatching(const std::vector<std::shared_ptr<ISequence>> &values, const std::shared_ptr<ISequence> &toMatch) {
        auto numToRemove = 0U;
        for (auto &&value : values) {
            if (value == toMatch) // Specifically uses identity
            {
                numToRemove++;
            }
        }
        return numToRemove;
    }
};

} // namespace opencmw::disruptor
