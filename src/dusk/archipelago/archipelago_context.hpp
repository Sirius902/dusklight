#pragma once

#include <chrono>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "apclient.hpp"
#include "d/d_save.h"

namespace dusk::archi
{
    struct ReceivedItem {
        int index = -1;
        int itemId = -1;
        bool notify = false;
        int player = -1;
        int64_t location = -1;
    };

    class ArchipelagoContext {
    public:
        enum class ConnectionPhase {
            IDLE,
            CONNECTING,
            SLOT_CONNECTED,
            GENERATING,
            CONNECTED,
            ERROR,
            INVALID_SAVE,
        };

    private:
        struct TEMP_GameItemInfo {
            int itemId = -1;
            randomizer::logic::item::Importance importance = randomizer::logic::item::Importance::INVALID;
            std::string itemName;
        };

        struct TEMP_GameLocationInfo {
            int apId = -1;
            std::string locName;
        };

        struct GameLocationInfo {
            int itemId = -1;
            std::string itemName;
            std::string locationName;
            int64_t apLocationId = -1;
            bool collected = false;
        };

        std::vector<ReceivedItem> m_receivedItemsQueue;

        // AP Client
        std::unique_ptr<APDataPackageStore> m_dataPackageStore;
        std::unique_ptr<APClient> m_client;
        ConnectionPhase m_connectionPhase = ConnectionPhase::IDLE;
        std::string m_seedName;
        int m_slot = -1;
        std::string m_slotName;
        std::string m_password;

        // Connect timeout
        std::chrono::steady_clock::time_point m_connectStartTime;
        bool m_hasEverConnected = false;
        bool m_pendingDisconnect = false;

        // Per-save cursor
        uint64_t m_seedSlotKey = 0;
        dSv_reserve_c m_candidateSaveBlock{};
        int m_candidateFileNum = -1;
        std::set<int64_t> m_locallyObtainedThisSession;
        uint32_t m_resolvedIndexHighWater = 0;
        bool m_syncRequested = false;

        // Rando Data
        randomizer::seedgen::config::Config m_config;
        std::unique_ptr<randomizer::logic::world::World> m_archiWorld = nullptr;
        bool m_isUpdateLocations = false;
        bool m_isAllowUpdateLocations = false;
        bool m_needApplyServerState = false;
        bool m_isEnableDeathLink = false;

        // AP Data
        std::unordered_map<std::string, GameLocationInfo> m_locationItemInfo;
        std::map<int64_t, bool> m_initLocationCollectState;
        std::string m_SettingsFile;
        bool m_isNeedPlayerDeath = false;
        bool m_isFromDeathLink = false;
        bool m_goalReached = false;

        // TEMP
        std::map<int, TEMP_GameItemInfo> m_apItemToGameItem;
        std::vector<TEMP_GameLocationInfo> m_apLocToGameLoc;

        void LoadTempItemInfo();

        void LoadTempLocationInfo();

        bool itemRecvImpl(int id, bool notify);

        int getItemIdFromApId(int apId);

        std::string getLocationNameFromApId(int apId) const;

        bool tryKillPlayer();

        void resolveReceivedItems();

        bool validateSaveCursor();
    public:
        ArchipelagoContext();

        // Config Getters/Setters

        static void SetServerIp(const std::string_view& ip, int file);
        static void SetSlotName(const std::string_view& name, int file);
        static void SetPassword(const std::string_view& pass, int file);

        static const std::string& GetServerIp(int file);
        static const std::string& GetSlotName(int file);
        static const std::string& GetPassword(int file);

        static std::string GetArchipelagoSeedName();

        static void GetSeedDirectoryPath(std::filesystem::path& outPath);

        static bool IsSeedHashArchipelago(const std::string& seedStr);

        static bool IsCurrentSeedHash(const std::string& seedStr);

        // Connection Handlers

        static void ConnectToServer(int file);

        static void DisconnectFromServer();

        static void ResetSession();

        static bool IsConnected();

        static void Poll();

        static ConnectionPhase GetConnectionPhase();

        // State Handlers

        static void Execute();

        static void UpdateCheckedLocations();

        static void SetNeedUpdateLocations(bool update);

        static void SetLocationChecked(int64_t locId, bool collected);

        static void applyServerLocationState();

        static bool IsReceivedLocationScouts();

        static void TryHandleDeathLink();

        static bool TryHandleGameComplete();

        // State Requesters

        static void SetCandidateSaveBlock(int fileNum, const void* saveData);

        static void InitApSaveBlock();

        static void RequestAllLocationScout();

        static void RequestPlayerDeath(bool isDeathLink = false);

        // AP -> Internal Rando Converters

        static bool GenerateConfigFromAP(randomizer::seedgen::config::Config& config, const std::string& settingsStr);

        static int GetItemAtLocation(const std::string& locName);

        static int GetItemAtLocation(int locId);

        static void CreateArchipelagoWorld();

        static void FillArchipelagoWorld();

        static void CreateRandomizerContext();

        static void LoadRandomizerContext();

        static bool GenerateLocalWorldData();

    };
} // dusk::archi
