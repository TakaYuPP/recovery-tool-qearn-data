#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include "./m256.h"
#include "keyUtils.h"
#include "K12AndKeyUtil.h"

constexpr uint64_t QEARN_MAX_LOCKS = 4194304;
constexpr uint64_t QEARN_MAX_EPOCHS = 4096;
constexpr uint64_t QEARN_MAX_USERS = 131072;

typedef m256i id;

template <typename T, uint64_t L>
struct array
{
private:
    static_assert(L && !(L & (L - 1)),
        "The capacity of the array must be 2^N."
        );

    T _values[L];

public:
    // Return number of elements in array
    static inline constexpr uint64_t capacity()
    {
        return L;
    }

    // Get element of array
    inline const T& get(uint64_t index) const
    {
        return _values[index & (L - 1)];
    }

    // Set element of array
    inline void set(uint64_t index, const T& value)
    {
        _values[index & (L - 1)] = value;
    }

    // Set content of array by copying memory (size must match)
    template <typename AT>
    inline void setMem(const AT& value)
    {
        static_assert(sizeof(_values) == sizeof(value), "This function can only be used if the overall size of both objects match.");
        // This if is resolved at compile time
        if (sizeof(_values) == 32)
        {
            // assignment uses __m256i intrinsic CPU functions which should be very fast
            *((id*)_values) = *((id*)&value);
        }
        else
        {
            // generic copying
            copyMemory(*this, value);
        }
    }

    // Set all elements to passed value
    inline void setAll(const T& value)
    {
        for (uint64_t i = 0; i < L; ++i)
            _values[i] = value;
    }

    // Set elements in range to passed value
    inline void setRange(uint64_t indexBegin, uint64_t indexEnd, const T& value)
    {
        for (uint64_t i = indexBegin; i < indexEnd; ++i)
            _values[i & (L - 1)] = value;
    }

    // Returns true if all elements of the range equal value (and range is valid).
    inline bool rangeEquals(uint64_t indexBegin, uint64_t indexEnd, const T& value) const
    {
        if (indexEnd > L || indexBegin > indexEnd)
            return false;
        for (uint64_t i = indexBegin; i < indexEnd; ++i)
        {
            if (!(_values[i] == value))
                return false;
        }
        return true;
    }
};


// Divide a by b, but return 0 if b is 0 (rounding to lower magnitude in case of integers)
template <typename T1, typename T2>
inline static auto safe_div(T1 a, T2 b) -> decltype(a / b)
{
    return (b == 0) ? 0 : (a / b);
}

struct RoundInfo {

    uint64_t _totalLockedAmount;            // The initial total locked amount in any epoch.  Max Epoch is 65535
    uint64_t _epochBonusAmount;             // The initial bonus amount per an epoch.         Max Epoch is 65535 

};

struct EpochIndexInfo {

    uint32_t startIndex;
    uint32_t endIndex;
};

struct LockInfo {

    uint64_t _lockedAmount;
    id ID;
    uint32_t _lockedEpoch;

};

struct HistoryInfo {

    uint64_t _unlockedAmount;
    uint64_t _rewardedAmount;
    id _unlockedID;

};

struct StatsInfo {

    uint64_t burnedAmount;
    uint64_t boostedAmount;
    uint64_t rewardedAmount;

};

array<RoundInfo, QEARN_MAX_EPOCHS> _initialRoundInfo;
array<RoundInfo, QEARN_MAX_EPOCHS> _currentRoundInfo;
array<EpochIndexInfo, QEARN_MAX_EPOCHS> _epochIndex;
array<LockInfo, QEARN_MAX_LOCKS> locker;
array<LockInfo, QEARN_MAX_LOCKS> newLocker;
array<HistoryInfo, QEARN_MAX_USERS> earlyUnlocker;
array<HistoryInfo, QEARN_MAX_USERS> fullyUnlocker;

uint32_t _earlyUnlockedCnt;
uint32_t _fullyUnlockedCnt;
array<StatsInfo, QEARN_MAX_EPOCHS> statsInfo;

// Function to read old state from a file
void readOldState(const std::string& filename) {

    std::ifstream infile(filename, std::ios::binary);

    if (!infile) {
        throw std::runtime_error("Failed to open the old state file.");
    }

    infile.read(reinterpret_cast<char*>(&_initialRoundInfo), sizeof(_initialRoundInfo));
    infile.read(reinterpret_cast<char*>(&_currentRoundInfo), sizeof(_currentRoundInfo));
    infile.read(reinterpret_cast<char*>(&_epochIndex), sizeof(_epochIndex));
    infile.read(reinterpret_cast<char*>(&locker), sizeof(locker));
    infile.read(reinterpret_cast<char*>(&earlyUnlocker), sizeof(earlyUnlocker));
    infile.read(reinterpret_cast<char*>(&fullyUnlocker), sizeof(fullyUnlocker));
    infile.read(reinterpret_cast<char*>(&_earlyUnlockedCnt), sizeof(_earlyUnlockedCnt));
    infile.read(reinterpret_cast<char*>(&_fullyUnlockedCnt), sizeof(_fullyUnlockedCnt));
    infile.read(reinterpret_cast<char*>(&statsInfo), sizeof(statsInfo));
}

// Function to write new state to a file
void writeNewState(const std::string& filename) {

    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile) {
        throw std::runtime_error("Failed to open the new state file.");
    }

    outfile.write(reinterpret_cast<const char*>(&_initialRoundInfo), sizeof(_initialRoundInfo));
    outfile.write(reinterpret_cast<const char*>(&_currentRoundInfo), sizeof(_currentRoundInfo));
    outfile.write(reinterpret_cast<const char*>(&_epochIndex), sizeof(_epochIndex));
    outfile.write(reinterpret_cast<const char*>(&locker), sizeof(locker));
    outfile.write(reinterpret_cast<const char*>(&earlyUnlocker), sizeof(earlyUnlocker));
    outfile.write(reinterpret_cast<const char*>(&fullyUnlocker), sizeof(fullyUnlocker));
    outfile.write(reinterpret_cast<const char*>(&_earlyUnlockedCnt), sizeof(_earlyUnlockedCnt));
    outfile.write(reinterpret_cast<const char*>(&_fullyUnlockedCnt), sizeof(_fullyUnlockedCnt));
    outfile.write(reinterpret_cast<const char*>(&statsInfo), sizeof(statsInfo));

    if (!outfile) {
        throw std::runtime_error("Failed to write id to the file.");
    }
    outfile.close();
}

int main() {
    try {
        // File paths
        const std::string oldStateFile = "contract0009.174";
        const std::string newStateFile = "contract0009.174";

        // Read the old state
        readOldState(oldStateFile);

        uint64_t totalLockedAmount = 0;
        uint64_t totalEpochBonusAmount = 0;
        for(uint32_t i = 138 ; i <= 173; i++)
        {
            totalLockedAmount += _currentRoundInfo.get(i)._totalLockedAmount;
            totalEpochBonusAmount += _currentRoundInfo.get(i)._epochBonusAmount;
        }

        std::cout << "Total locked amount: " << totalLockedAmount << std::endl;
        std::cout << "Total epoch bonus amount: " << totalEpochBonusAmount << std::endl;
        std::cout << "Total balance amount: " << totalLockedAmount + totalEpochBonusAmount << std::endl;

        // Create and open a CSV file
        std::ofstream lockerFile("locker.csv");

        if (!lockerFile) {
            std::cerr << "Error creating file!" << std::endl;
            return 1;
        }

        // Write header
        lockerFile << "lockedAmount,Id,lockedEpoch\n";

        char idStr1[128] = {0};
        // Write some data rows
        uint32_t cur_cnt = 0;
        uint32_t newLockerCnt = 0;

        uint64_t totalLockedAmountInEpoch172 = 0;

        for(uint32_t i = 138 ; i <= 173; i++)
        {
            uint32_t startIndex = _epochIndex.get(i).startIndex;
            uint32_t endIndex = _epochIndex.get(i).endIndex;
            uint32_t cnt = 0;

            for(uint32_t j = startIndex; j < endIndex; j++)
            {
                if(locker.get(j)._lockedAmount == 0)
                {
                    cnt++;
                    continue;
                }
                newLocker.set(newLockerCnt++, locker.get(j));
                if(i == 172)
                {
                    totalLockedAmountInEpoch172 += locker.get(j)._lockedAmount;
                }
            }

            EpochIndexInfo tempEpochIndex;
            tempEpochIndex.startIndex = startIndex - cur_cnt;
            tempEpochIndex.endIndex = endIndex - cur_cnt - cnt;
            _epochIndex.set(i, tempEpochIndex);
            cur_cnt += cnt;
        }

        for(uint32_t i = 0; i < QEARN_MAX_USERS; i++){
            if(i < newLockerCnt){
                locker.set(i, newLocker.get(i));
                char idStr[128] = {0};
                getIdentityFromPublicKey(locker.get(i).ID.m256i_u8, idStr, false);
                lockerFile << locker.get(i)._lockedAmount << "," << idStr << "," << locker.get(i)._lockedEpoch << std::endl;
            }
            else{
                locker.set(i, LockInfo{0, id::zero(), 0});
            }
        }

        lockerFile.close();
        std::cout << "CSV lockerFile created successfully!" << std::endl;

        std::cout << "Total locked amount in epoch 172: " << totalLockedAmountInEpoch172 << std::endl;
        RoundInfo tempRoundInfo;
        tempRoundInfo._totalLockedAmount = totalLockedAmountInEpoch172;
        tempRoundInfo._epochBonusAmount = _currentRoundInfo.get(172)._epochBonusAmount - totalLockedAmountInEpoch172;
        _currentRoundInfo.set(172, tempRoundInfo);
        _initialRoundInfo.set(172, tempRoundInfo);

        std::ofstream roundInfoFile("roundInfo.csv");
        roundInfoFile << "Epoch,totalLockedAmount,epochBonusAmount\n";
        for(uint32_t i = 138 ; i <= 173; i++)
        {
            roundInfoFile << i << "," << _currentRoundInfo.get(i)._totalLockedAmount << "," << _currentRoundInfo.get(i)._epochBonusAmount << std::endl;
        }
        for(uint32_t i = 138 ; i <= 173; i++)
        {
            roundInfoFile << i << "," << _initialRoundInfo.get(i)._totalLockedAmount << "," << _initialRoundInfo.get(i)._epochBonusAmount << std::endl;
        }
        roundInfoFile.close();
        std::cout << "CSV roundInfoFile created successfully!" << std::endl;

        std::ofstream epochIndexFile("epochIndex.csv");

        // Write header
        epochIndexFile << "startIndex,endIndex\n";

        for(uint32_t i = 138 ; i <= 173; i++)
        {
            epochIndexFile << _epochIndex.get(i).startIndex << "," << _epochIndex.get(i).endIndex << std::endl;
        }

        epochIndexFile.close();

        std::cout << "CSV epochIndexFile created successfully!" << std::endl;

        // Write the new state to a file
        writeNewState(newStateFile);

        std::cout << "Migration completed successfully. New state saved to: " << newStateFile << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}