#include <dusk/archipelago/archipelago_context.hpp>

#include <chrono>
#include <cstring>
#include <ctime>
#include <list>
#include <set>

#include "apuuid.hpp"
#include "defaultdatapackagestore.hpp"
#include "d/d_item.h"
#include "d/d_save.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "dusk/config.hpp"
#include "dusk/logging.h"
#include "dusk/randomizer/game/randomizer_context.hpp"
#include "dusk/randomizer/game/tools.h"
#include "dusk/randomizer/game/verify_item_functions.h"
#include "dusk/randomizer/generator/logic/hints.hpp"
#include "dusk/ui/rando_config.hpp"
#include "dusk/ui/ui.hpp"

namespace dusk::archi
{

static constexpr int ARCHI_ITEM_OFFSET = 2320000;

static uint64_t fnv1a64(std::string_view data) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (auto c : data) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static uint64_t computeSeedSlotKey(const std::string& seedName, const std::string& slotName) {
    std::string combined = seedName;
    combined.push_back('\0');
    combined += slotName;
    return fnv1a64(combined);
}

struct SettingsNameConvert {
    static constexpr std::string kDefaultYes = "On";
    static constexpr std::string kDefaultNo = "Off";

    std::string apName;
    std::string dusklightName;
    std::vector<std::pair<std::string, std::string>> optionsConvert;

    const std::string& tryGetOptionConvert(const std::string& option) const {
        if (optionsConvert.empty()) {
            if (option == "Yes" || option == "true")
                return kDefaultYes;
            if (option == "No" || option == "false")
                return kDefaultNo;
            return option;
        }

        for (const auto& value : optionsConvert) {
            if (value.first == option) {
                return value.second;
            }
        }
        return option;
    }
};

static auto sArchiSettingToDusklight = std::to_array<SettingsNameConvert>({
    {"", ""},
    {"Golden Bugs Shuffled", "Golden Bugs"},
    {"Sky Chracters Shuffled", "Sky Characters"},
    {"Sky Characters Shuffled", "Sky Characters"},
    {"NPC Items Shuffled", "Gifts From NPCs"},
    {"Shop Items Shuffled", "Shop Items"},
    {"Hidden Skills Shuffled", "Hidden Skills"},
    {"Skip Prologue", "Skip Prologue"},
    {"Faron Twilight Cleared", "Faron Twilight Cleared"},
    {"Eldin Twilight Cleared", "Eldin Twilight Cleared"},
    {"Lanayru Twilight Cleared", "Lanayru Twilight Cleared"},
    {"Skip MDH", "Skip Midna's Desparate Hour"},
    {"Open Map", "Unlock Map Regions"},
    {"Increase Wallet", "Logic Increase Wallet Capacity"},
    {"Transform Anywhere", "Logic Transform Anywhere"},
    {"Bonks do Damage", "Bonks Do Damage"},
    {"Lakebed Entrance Requirements", "Lakebed Does Not Require Water Bombs"},
    {"Arbiters Grounds Entrance Requirements", "Arbiters Does Not Require Bulblin Camp"},
    {"Snowpeak Entrance Requirements", "Snowpeak Does Not Require Reekfish Scent"},
    {"City in the Sky Entrance Requirements", "City Does Not Require Filled Skybook"},
    {"Goron Mines Entrance Requirements", "Goron Mines Entrance"},
    {"Palace of Twilight Requirements", "Palace of Twilight Requirements"},
    {"Faron Woods Logic", "Faron Woods Logic"},
{"Starting ToD", "Starting Time of Day"},
   {"Skip Major Cutscenes", "Skip Major Cutscenes"},
{"Skip Minor Cutscenes", "Skip Minor Cutscenes"},
   {"Open Door of Time", "Open Door of Time"},

    {"Dungeon Rewards Progression", "Dungeon Rewards Can Be Anywhere", {
         // these two are functionally identical in terms of tracker logic, so treat it as such
         {"Anything", "On"},
         {"Any Progressive", "On"},
         {"Vanilla", "Off"},
     }},
    {"Small Key Settings", "Small Keys", {
         {"Startwith", "Keysy"},
     }},
    {"Big Key Settings", "Big Keys", {
         {"Startwith", "Keysy"},
     }},
    {"Map and Compass Settings", "Maps and Compasses", {
         {"Startwith", "Start With"},
     }},
    {"Trap Frequency", "Trap Item Frequency", {
         {"No Traps", "None"},
     }},
    {"Damage Magnification", "Logic Damage Multiplier", {
         {"Ohko", "OHKO"},
     }},
    {"Logic Settings", "Logic Rules", {
         {"Glitchless", "All Locations Reachable"},
         {"Glitched", "Beatable Only"},
    }},
    {"Poes Shuffled", "Poe Souls", {
        {"Yes", "All"},
        {"No", "Vanilla"}
    }}
});

ArchipelagoContext& instance() {
    static ArchipelagoContext instance;
    return instance;
}

const SettingsNameConvert& GetAPSettingNameConvert(const std::string& apSettingName) {
    for (const auto& entry : sArchiSettingToDusklight) {
        if (entry.apName == apSettingName)
            return entry;
    }
    return sArchiSettingToDusklight[0];
}

void ArchipelagoContext::LoadTempItemInfo() {
    auto itemDataTree = LOAD_EMBED_YAML(RANDO_DATA_PATH "items.yaml");
    for (const auto& itemNode : itemDataTree) {
        if (!itemNode["APItemId"]) {
            DuskLog.warn("Item {} missing APItemId field!", itemNode["Name"].as<std::string>());
            continue;
        }
        auto apItemId = itemNode["APItemId"].as<int>();

        if (apItemId == -1)
            continue;

        auto id = itemNode["Id"].as<int>();
        auto importance = randomizer::logic::item::ImportanceFromStr(itemNode["Importance"].as<std::string>());
        auto itemName = itemNode["Name"].as<std::string>();

        m_apItemToGameItem[apItemId] = {
            id,
            importance,
            itemName
        };
    }

    // add temporary replacement IDs for items not included in the base rando

    m_apItemToGameItem[16] = {  // Water Bombs (3)
        0x16,
        randomizer::logic::item::Importance::JUNK,
        "Water Bombs 5"
    };

    m_apItemToGameItem[20] = {  // Bomblings (3)
        0x1A,
        randomizer::logic::item::Importance::JUNK,
        "Bomblings 5"
    };
}

void ArchipelagoContext::LoadTempLocationInfo() {
    m_apLocToGameLoc.clear();
    auto locDataTree = LOAD_EMBED_YAML(RANDO_DATA_PATH "locations.yaml");
    for (const auto& locNode : locDataTree) {
        const auto& metadata = locNode["Metadata"];
        auto locationName =  locNode["Name"].as<std::string>();

        if (!metadata.IsMap()) {
            DuskLog.warn("Location {} missing correct Metadata field!", locationName);
            continue;
        }

        if (!metadata["APLocationId"]) {
            DuskLog.warn("Location {} missing APLocationId field!", locationName);
            continue;
        }

        auto apLocationId = metadata["APLocationId"].as<int>();

        if (apLocationId == -1)
            continue;

        m_apLocToGameLoc.push_back({
            apLocationId,
            locationName
        });
    }
}

bool ArchipelagoContext::itemRecvImpl(int id, bool notify) {
    if (!m_apItemToGameItem.contains(id)) {
        DuskLog.warn("[AP] Got an invalid Item Id: {}", id);
        return true;
    }

    m_isAllowUpdateLocations = true;

    auto& item = m_apItemToGameItem[id];

    bool applied;
    if (notify && item.importance == randomizer::logic::item::Importance::MAJOR) {
        DuskLog.info("[AP] Adding Item: {}", item.itemName);
        applied = g_randomizerState.addItemToEventQueue(item.itemId);
    } else {
        DuskLog.info("[AP] Silently Adding Item: {}", item.itemName);
        execItemGet(item.itemId);
        applied = true;
    }

    m_isAllowUpdateLocations = false;
    return applied;
}

int ArchipelagoContext::getItemIdFromApId(int apId) {
    if (!m_apItemToGameItem.contains(apId)) {
        DuskLog.warn("Got an invalid Item Id: {}", apId);
        return -1;
    }

    auto& item = m_apItemToGameItem[apId];

    return item.itemId;
}

std::string ArchipelagoContext::getLocationNameFromApId(int apId) const {
    for (const auto& entry : m_apLocToGameLoc) {
        if (entry.apId == apId)
            return entry.locName;
    }
    return "";
}

bool ArchipelagoContext::tryKillPlayer() {
    if (!m_isNeedPlayerDeath)
        return false;

    auto linkActor = daAlink_getAlinkActorClass();

    if (!linkActor)
        return false;

    switch (linkActor->mProcID) {
        case daAlink_c::PROC_WAIT:
        case daAlink_c::PROC_TIRED_WAIT:
        case daAlink_c::PROC_MOVE:
        case daAlink_c::PROC_WOLF_WAIT:
        case daAlink_c::PROC_WOLF_TIRED_WAIT:
        case daAlink_c::PROC_WOLF_MOVE:
        case daAlink_c::PROC_ATN_MOVE:
        case daAlink_c::PROC_WOLF_ATN_AC_MOVE: {
            if (linkActor->checkEventRun())
                break;

            if (linkActor->getEventId() != 0)
                break;

            dComIfGs_setLife(0);
            m_isNeedPlayerDeath = false;

            return true;
        }
        default:
            break;
    }

    return false;
}

ArchipelagoContext::ArchipelagoContext() = default;

void ArchipelagoContext::SetServerIp(const std::string_view& ip, int file) {
    getSettings().archipelago.savesServerIP[file].setValue(std::string(ip));
}

void ArchipelagoContext::SetSlotName(const std::string_view& name, int file) {
    getSettings().archipelago.savesSlotName[file].setValue(std::string(name));
}

void ArchipelagoContext::SetPassword(const std::string_view& pass, int file) {
    getSettings().archipelago.savesServerPass[file].setValue(std::string(pass));
}

const std::string& ArchipelagoContext::GetServerIp(int file) {
    return getSettings().archipelago.savesServerIP[file].getValue();
}

const std::string& ArchipelagoContext::GetSlotName(int file) {
    return getSettings().archipelago.savesSlotName[file].getValue();
}

const std::string& ArchipelagoContext::GetPassword(int file) {
    return getSettings().archipelago.savesServerPass[file].getValue();
}

std::string ArchipelagoContext::GetArchipelagoSeedName() {
    if (IsConnected()) {
        if (instance().m_seedName.empty()) {
            DuskLog.warn("Got an invalid Seed Name!");
        }
        return fmt::format("AP_{}", instance().m_seedName);
    }
    return "";
}

void ArchipelagoContext::GetSeedDirectoryPath(std::filesystem::path& outPath) {
    if (IsConnected()) {
        outPath = ui::GetRandomizerPath() / "archipelago" / GetArchipelagoSeedName();
    }
}

bool ArchipelagoContext::IsSeedHashArchipelago(const std::string& seedStr) {
    return seedStr.starts_with("AP_");
}

bool ArchipelagoContext::IsCurrentSeedHash(const std::string& seedStr) {
    return GetArchipelagoSeedName() == seedStr;
}

void ArchipelagoContext::SetCandidateSaveBlock(int fileNum, const void* saveData) {
    auto save = static_cast<const dSv_save_c*>(saveData);
    std::memcpy(&instance().m_candidateSaveBlock, &save->reserve, sizeof(dSv_reserve_c));
    instance().m_candidateFileNum = fileNum;
}

void ArchipelagoContext::InitApSaveBlock() {
    if (!IsConnected()) return;
    if (instance().m_seedSlotKey == 0) return;

    auto& save = g_dComIfG_gameInfo.info.getSavedata();
    save.reserve.initAp(instance().m_seedSlotKey, instance().m_seedName.c_str());
    DuskLog.info("[AP] Initialized AP save block for new file.");
}

void ArchipelagoContext::ConnectToServer(int file) {
    config::Save();

    instance().LoadTempItemInfo();
    instance().LoadTempLocationInfo();

    instance().m_slotName = GetSlotName(file);
    instance().m_password = GetPassword(file);

    auto uri = GetServerIp(file);

    auto randoPath = ui::GetRandomizerPath();
    std::filesystem::create_directories(randoPath);
    auto uuid = ap_get_uuid((randoPath / "ap_uuid.dat").string(), uri);

    instance().m_client.reset();
    instance().m_dataPackageStore = std::make_unique<DefaultDataPackageStore>();
    instance().m_client = std::make_unique<APClient>(
        uuid, "Twilight Princess", uri, "", instance().m_dataPackageStore.get());

    auto& client = *instance().m_client;

    client.set_room_info_handler([]() {
        instance().m_seedName = instance().m_client->get_seed();
        std::list<std::string> tags;
        if (instance().m_isEnableDeathLink)
            tags.push_back("DeathLink");
        instance().m_client->ConnectSlot(
            instance().m_slotName, instance().m_password, 0b111, tags);
    });

    client.set_slot_connected_handler([](const nlohmann::json& slotData) {
        instance().m_slot = instance().m_client->get_player_number();

        if (slotData.contains("Settings")) {
            const auto& settings = slotData["Settings"];
            instance().m_SettingsFile = settings.is_string() ? settings.get<std::string>() : settings.dump();
        } else {
            instance().m_SettingsFile = "";
        }

        if (slotData.contains("death_link")) {
            const auto& dl = slotData["death_link"];
            instance().m_isEnableDeathLink = dl.is_boolean() ? dl.get<bool>() : dl.get<int>() != 0;
        } else {
            instance().m_isEnableDeathLink = false;
        }

        if (instance().m_isEnableDeathLink) {
            instance().m_client->ConnectUpdate(false, 0, true, {"DeathLink"});
        }

        instance().m_receivedItemsQueue.clear();
        instance().m_syncRequested = false;
        instance().m_needApplyServerState = true;
        instance().m_isUpdateLocations = true;

        instance().m_seedSlotKey = computeSeedSlotKey(
            instance().m_seedName, instance().m_slotName);

        auto& candidate = instance().m_candidateSaveBlock;
        if (candidate.isApValid() &&
            candidate.getApSeedSlotKey() != instance().m_seedSlotKey) {
            DuskLog.error("[AP] Save file is bound to a different seed/slot.");
            instance().m_connectionPhase = ConnectionPhase::INVALID_SAVE;
            return;
        }

        if (instance().m_goalReached) {
            instance().m_client->StatusUpdate(APClient::ClientStatus::GOAL);
        } else {
            instance().m_client->StatusUpdate(APClient::ClientStatus::PLAYING);
        }

        instance().m_connectionPhase = ConnectionPhase::SLOT_CONNECTED;
        instance().m_connectStartTime = std::chrono::steady_clock::now();
        RequestAllLocationScout();
    });

    client.set_slot_refused_handler([](const std::list<std::string>& errors) {
        for (const auto& err : errors) {
            DuskLog.error("[AP] Connection refused: {}", err);
        }
        instance().m_connectionPhase = ConnectionPhase::ERROR;
        instance().m_pendingDisconnect = true;
    });

    client.set_socket_error_handler([](const std::string& error) {
        DuskLog.error("[AP] Socket error: {}", error);
    });

    client.set_socket_disconnected_handler([]() {
        DuskLog.info("[AP] Socket disconnected.");
        auto phase = instance().m_connectionPhase;
        if (phase == ConnectionPhase::ERROR)
            return;
        if (phase != ConnectionPhase::IDLE)
            instance().m_connectionPhase = ConnectionPhase::CONNECTING;
    });

    client.set_items_received_handler([](const std::list<APClient::NetworkItem>& items) {
        auto& inst = instance();

        for (const auto& item : items) {
            if (item.index < 0) continue;

            int relativeId = static_cast<int>(item.item - ARCHI_ITEM_OFFSET);
            bool notify = (inst.m_connectionPhase == ConnectionPhase::CONNECTED);

            inst.m_receivedItemsQueue.push_back({
                static_cast<int>(item.index),
                relativeId,
                notify,
                static_cast<int>(item.player),
                item.location
            });
        }
    });

    client.set_location_info_handler([](const std::list<APClient::NetworkItem>& items) {
        DuskLog.info("Got {} Location Scouts from Server.", items.size());

        for (const auto& item : items) {
            int parsedItemId;
            std::string parsedItemName;
            if (item.player == instance().m_slot) {
                int adjustedId = static_cast<int>(item.item - ARCHI_ITEM_OFFSET);

                if (instance().m_apItemToGameItem.contains(adjustedId)) {
                    auto& itemInfo = instance().m_apItemToGameItem[adjustedId];
                    parsedItemId = itemInfo.itemId;
                    parsedItemName = itemInfo.itemName;
                } else {
                    parsedItemId = -1;
                    parsedItemName = "Unknown";
                }
            } else {
                parsedItemId = dItemNo_Randomizer_ARCHIPELAGO_ITEM_e;
                parsedItemName = "Archipelago Item";
            }
            int locationId = static_cast<int>(item.location - ARCHI_ITEM_OFFSET);

            auto locName = instance().getLocationNameFromApId(locationId);

            if (locName.empty()) {
                DuskLog.info("No location with ID {} found.", locationId);
                continue;
            }

            bool collected = false;
            if (instance().m_initLocationCollectState.contains(item.location))
                collected = instance().m_initLocationCollectState[item.location];

            instance().m_locationItemInfo[locName] = {
                parsedItemId,
                parsedItemName,
                locName,
                item.location,
                collected
            };
        }

        if (instance().m_connectionPhase == ConnectionPhase::SLOT_CONNECTED) {
            instance().m_connectionPhase = ConnectionPhase::GENERATING;
        }
    });

    client.set_location_checked_handler([](const std::list<int64_t>& locations) {
        for (auto locId : locations) {
            DuskLog.info("Location Checked Callback Called! Location: {}", locId);
            SetLocationChecked(locId, true);
        }
    });

    client.set_bounced_handler([](const nlohmann::json& bounce) {
        if (!bounce.contains("tags") || !bounce["tags"].is_array()) return;

        bool isDeathLink = false;
        for (const auto& tag : bounce["tags"]) {
            if (tag == "DeathLink") {
                isDeathLink = true;
                break;
            }
        }
        if (!isDeathLink) return;

        if (bounce.contains("data") && bounce["data"].contains("source")) {
            const auto& src = bounce["data"]["source"];
            if (src.is_string()) {
                if (src.get<std::string>() == instance().m_client->get_slot()) return;
                DuskLog.info("Player {} sent death link.", src.get<std::string>());
            } else if (src.is_number_integer()) {
                if (src.get<int>() == instance().m_client->get_player_number()) return;
                DuskLog.info("Player {} sent death link.", src.get<int>());
            }
        }

        RequestPlayerDeath(true);
    });

    client.set_print_json_handler([](const APClient::PrintJSONArgs& args) {
        auto text = instance().m_client->render_json(args.data);

        if (args.type == "ItemSend" && args.receiving && args.item) {
            if (instance().m_client->slot_concerns_self(*args.receiving)) {
                ui::push_toast({
                    .title = "Item Received",
                    .content = text,
                    .duration = std::chrono::seconds(3),
                });
            } else if (instance().m_client->slot_concerns_self(args.item->player)) {
                ui::push_toast({
                    .title = "Item Sent",
                    .content = text,
                    .duration = std::chrono::seconds(3),
                });
            }
        }

        DuskLog.info("[AP] {}", text);
    });

    instance().m_connectionPhase = ConnectionPhase::CONNECTING;
    instance().m_connectStartTime = std::chrono::steady_clock::now();
    instance().m_hasEverConnected = false;
    instance().m_pendingDisconnect = false;
}

void ArchipelagoContext::ResetSession() {
    instance().m_client.reset();
    instance().m_seedName.clear();
    instance().m_slot = -1;
    instance().m_receivedItemsQueue.clear();
    instance().m_locationItemInfo.clear();
    instance().m_initLocationCollectState.clear();
    instance().m_isEnableDeathLink = false;
    instance().m_pendingDisconnect = false;
    instance().m_isNeedPlayerDeath = false;
    instance().m_isFromDeathLink = false;
    instance().m_seedSlotKey = 0;
    instance().m_SettingsFile.clear();
    instance().m_locallyObtainedThisSession.clear();
    instance().m_resolvedIndexHighWater = 0;
    instance().m_syncRequested = false;
}

void ArchipelagoContext::DisconnectFromServer() {
    ResetSession();
    instance().m_connectionPhase = ConnectionPhase::IDLE;
}

bool ArchipelagoContext::IsConnected() {
    auto phase = instance().m_connectionPhase;
    return phase >= ConnectionPhase::SLOT_CONNECTED &&
           phase != ConnectionPhase::ERROR &&
           phase != ConnectionPhase::INVALID_SAVE;
}

void ArchipelagoContext::Poll() {
    auto& inst = instance();
    if (!inst.m_client) return;

    inst.m_client->poll();

    if (inst.m_pendingDisconnect) {
        ResetSession();
        return;
    }

    if (!inst.m_hasEverConnected &&
        (inst.m_connectionPhase == ConnectionPhase::CONNECTING ||
         inst.m_connectionPhase == ConnectionPhase::SLOT_CONNECTED)) {
        auto elapsed = std::chrono::steady_clock::now() - inst.m_connectStartTime;
        if (elapsed > std::chrono::seconds(20)) {
            DuskLog.error("[AP] Connection timed out.");
            inst.m_connectionPhase = ConnectionPhase::ERROR;
            ResetSession();
            return;
        }
    }

    if (inst.m_connectionPhase == ConnectionPhase::GENERATING) {
        if (!GenerateLocalWorldData()) {
            DuskLog.error("[AP] World generation failed.");
            inst.m_connectionPhase = ConnectionPhase::ERROR;
            ResetSession();
            return;
        }

        if (!inst.m_client) return;

        inst.m_hasEverConnected = true;
        inst.m_connectionPhase = ConnectionPhase::CONNECTED;
    }
}

ArchipelagoContext::ConnectionPhase ArchipelagoContext::GetConnectionPhase() {
    return instance().m_connectionPhase;
}

bool ArchipelagoContext::validateSaveCursor() {
    auto dataNum = dComIfGs_getDataNum();
    auto& save = g_dComIfG_gameInfo.info.getSavedata();
    auto& reserve = save.reserve;

    if (!reserve.isApValid()) {
        if (instance().m_seedSlotKey != 0) {
            reserve.initAp(instance().m_seedSlotKey, instance().m_seedName.c_str());
            DuskLog.info("[AP] Initialized AP save block for file {}.", dataNum);
        }
        return true;
    }

    if (reserve.getApSeedSlotKey() != instance().m_seedSlotKey) {
        DuskLog.error("[AP] Save file seed-slot key mismatch.");
        return false;
    }

    u32 cursor = reserve.getApAppliedCount();
    if (cursor < instance().m_resolvedIndexHighWater && !instance().m_syncRequested) {
        DuskLog.info("[AP] Save cursor {} < high-water {}, requesting resync.",
                     cursor, instance().m_resolvedIndexHighWater);
        if (instance().m_client && instance().m_client->Sync()) {
            instance().m_syncRequested = true;
        }
    } else if (cursor >= instance().m_resolvedIndexHighWater) {
        instance().m_syncRequested = false;
    }

    return true;
}

void ArchipelagoContext::resolveReceivedItems() {
    auto& inst = instance();
    if (inst.m_receivedItemsQueue.empty()) return;

    if (!randomizer_isSafeForItemGrant()) return;

    auto& save = g_dComIfG_gameInfo.info.getSavedata();
    auto& reserve = save.reserve;
    u32 cursor = reserve.getApAppliedCount();

    size_t resolved = 0;
    for (auto& entry : inst.m_receivedItemsQueue) {
        if (entry.index < 0) {
            resolved++;
            continue;
        }

        if (static_cast<u32>(entry.index) < cursor) {
            resolved++;
            continue;
        }

        if (entry.notify && entry.location != -1 &&
            inst.m_locallyObtainedThisSession.contains(entry.location)) {
            u32 newCursor = static_cast<u32>(entry.index + 1);
            if (newCursor > cursor) {
                cursor = newCursor;
                reserve.setApAppliedCount(cursor);
            }
            if (newCursor > inst.m_resolvedIndexHighWater)
                inst.m_resolvedIndexHighWater = newCursor;
            resolved++;
            continue;
        }

        if (!inst.itemRecvImpl(entry.itemId, entry.notify)) {
            break;
        }

        u32 newCursor = static_cast<u32>(entry.index + 1);
        if (newCursor > cursor) {
            cursor = newCursor;
            reserve.setApAppliedCount(cursor);
        }
        if (newCursor > inst.m_resolvedIndexHighWater)
            inst.m_resolvedIndexHighWater = newCursor;
        resolved++;
    }

    if (resolved > 0) {
        inst.m_receivedItemsQueue.erase(
            inst.m_receivedItemsQueue.begin(),
            inst.m_receivedItemsQueue.begin() + static_cast<ptrdiff_t>(resolved));
    }
}

void ArchipelagoContext::Execute() {
    if (instance().m_connectionPhase != ConnectionPhase::CONNECTED) return;

    if (!randomizer_isSafeForItemGrant()) {
        if (instance().tryKillPlayer()) return;
        return;
    }

    instance().resolveReceivedItems();

    if (instance().tryKillPlayer()) return;

    if (instance().m_needApplyServerState) {
        applyServerLocationState();
        instance().m_needApplyServerState = false;
    }

    if (instance().m_isUpdateLocations) {
        UpdateCheckedLocations();
        instance().m_isUpdateLocations = false;
    }
}

void ArchipelagoContext::UpdateCheckedLocations() {
    auto& world = instance().m_archiWorld;
    if (!world) return;

    std::list<int64_t> batch;

    for (auto location : world->GetAllLocations()) {
        // skip locations that aren't progression, which are locations that just aren't randomized
        if (!location->IsProgression()) {
            continue;
        }

        auto locName = location->GetName();

        if (!instance().m_locationItemInfo.contains(locName)) {
            DuskLog.debug("No item found for ({}).", locName);
            continue;
        }

        auto& cachedLocData = instance().m_locationItemInfo[locName];

        bool isCollected = isLocationObtained(location);

        if (isCollected && !cachedLocData.collected) {
            cachedLocData.collected = true;
            batch.push_back(cachedLocData.apLocationId);
            instance().m_locallyObtainedThisSession.insert(cachedLocData.apLocationId);
        }
    }

    if (!batch.empty() && instance().m_client) {
        try {
            instance().m_client->LocationChecks(batch);
        } catch (const std::exception& e) {
            DuskLog.error("[AP] Failed to send LocationChecks: {}", e.what());
        }
    }
}

void ArchipelagoContext::SetNeedUpdateLocations(bool update) {
    if (!instance().m_isAllowUpdateLocations)
        instance().m_isUpdateLocations = update;
}

void ArchipelagoContext::SetLocationChecked(int64_t locId, bool collected) {
    if (!IsReceivedLocationScouts()) {
        instance().m_initLocationCollectState[locId] = collected;
        return;
    }

    if (!collected) return;

    for (auto& [locName, locInfo] : instance().m_locationItemInfo) {
        if (locInfo.apLocationId == locId) {
            if (!locInfo.collected) {
                locInfo.collected = true;
                instance().m_needApplyServerState = true;
            }
            return;
        }
    }
}

void ArchipelagoContext::applyServerLocationState() {
    auto& world = instance().m_archiWorld;
    if (!world) return;

    for (const auto& [locName, locInfo] : instance().m_locationItemInfo) {
        if (!locInfo.collected) continue;

        auto location = world->GetLocation(locInfo.locationName, true);
        if (!location || !location->IsProgression())
            continue;

        setLocationCollected(location, true);
    }
}

bool ArchipelagoContext::IsReceivedLocationScouts() {
    return !instance().m_locationItemInfo.empty();
}

bool ArchipelagoContext::IsApLocation(const std::string& locName) {
    return instance().m_locationItemInfo.contains(locName);
}

void ArchipelagoContext::TryHandleDeathLink() {
    bool wasFromDeathLink = instance().m_isFromDeathLink;
    instance().m_isFromDeathLink = false;
    instance().m_isNeedPlayerDeath = false;

    if (!instance().m_client) return;
    if (!instance().m_isEnableDeathLink) return;
    if (wasFromDeathLink) return;

    try {
        nlohmann::json deathData = {
            {"time", instance().m_client->get_server_time()},
            {"cause", fmt::format("{} was unable to become the Hero of Twilight.",
                                  instance().m_client->get_slot())},
            {"source", instance().m_client->get_slot()}
        };
        instance().m_client->Bounce(deathData, {}, {}, {"DeathLink"});
    } catch (const std::exception& e) {
        DuskLog.error("[AP] Failed to send DeathLink: {}", e.what());
    }
}

bool ArchipelagoContext::TryHandleGameComplete() {
    instance().m_goalReached = true;
    if (!instance().m_client) return false;
    try {
        instance().m_client->StatusUpdate(APClient::ClientStatus::GOAL);
    } catch (const std::exception& e) {
        DuskLog.error("[AP] Failed to send GOAL: {}", e.what());
    }
    return true;
}

void ArchipelagoContext::RequestAllLocationScout() {
    if (!instance().m_client) return;
    std::list<int64_t> locations;
    for (const auto& entry : instance().m_apLocToGameLoc) {
        locations.push_back(ARCHI_ITEM_OFFSET + entry.apId);
    }

    instance().m_client->LocationScouts(locations, 0);
}

void ArchipelagoContext::RequestPlayerDeath(bool isDeathLink) {
    instance().m_isNeedPlayerDeath = true;
    instance().m_isFromDeathLink = isDeathLink;
}

bool ArchipelagoContext::GenerateConfigFromAP(randomizer::seedgen::config::Config& config, const std::string& settingsStr) {
    YAML::Node apConfigYaml;
    try {
        apConfigYaml = YAML::Load(settingsStr);
    } catch (const YAML::Exception& e) {
        DuskLog.warn("Failed to parse AP Config: {}", e.what());
        return false;
    }

    config.SetSeed("Archipelago");
    randomizer::seedgen::settings::Settings& settings = config.GetSettings();

    // update settings using ap config
    for (const auto& apSettingEntry : apConfigYaml) {
      try {
        auto apSettingName = apSettingEntry.first.as<std::string>();
        std::string apSettingValue;
        if (apSettingEntry.second.IsScalar()) {
            apSettingValue = apSettingEntry.second.as<std::string>();
        } else {
            apSettingValue = apSettingEntry.second.as<std::string>("");
        }

        const auto& settingConvert = GetAPSettingNameConvert(apSettingName);

        if (!settingConvert.apName.empty()) {
            auto& setting = settings.GetMap().at(settingConvert.dusklightName);
            setting.SetCurrentOption(settingConvert.tryGetOptionConvert(apSettingValue));
        } else if (apSettingName == "Castle Requirements") {
            auto& setting = settings.GetMap().at("Hyrule Barrier Requirements");

            // ap assumes max mirror shards/fused shadows/dungeons, so update those settings as well

            if(apSettingValue == "Open")
                setting.SetCurrentOption("Open");
            else if(apSettingValue == "Vanilla")
                setting.SetCurrentOption("Vanilla");
            else if(apSettingValue == "Fused Shadows") {
                setting.SetCurrentOption("Fused Shadows");
                settings.GetMap().at("Hyrule Barrier Fused Shadows").SetCurrentOption("3");
            }else if(apSettingValue == "Mirror Shards") {
                setting.SetCurrentOption("Mirror Shards");
                settings.GetMap().at("Hyrule Barrier Mirror Shards").SetCurrentOption("4");
            }else if(apSettingValue == "All Dungeons") {
                setting.SetCurrentOption("Dungeons");
                settings.GetMap().at("Hyrule Barrier Dungeons").SetCurrentOption("8");
            }
        }else if (apSettingName == "Temple of Time Entrance Requirements") {
            auto& setting = settings.GetMap().at("Sacred Grove Does Not Require Skull Kid");
            auto& setting2 = settings.GetMap().at("Temple of Time Sword Requirement");

            if(apSettingValue == "Closed") {
                setting.SetCurrentOption("Off");
                setting2.SetCurrentOption("Master Sword");
            }else if (apSettingValue == "Open Grove") {
                setting.SetCurrentOption("On");
                setting2.SetCurrentOption("Master Sword");
            }else if (apSettingValue == "Open") {
                setting.SetCurrentOption("On");
                setting2.SetCurrentOption("None");
            }
        } else {
            DuskLog.debug("Missing Setting: {} Value: {}", apSettingName, apSettingValue);
        }
      } catch (const std::exception& e) {
        DuskLog.warn("Error applying AP setting: {}", e.what());
      }
    }

    return true;
}

int ArchipelagoContext::GetItemAtLocation(const std::string& locName) {
    if (!instance().m_locationItemInfo.contains(locName)) {
        DuskLog.warn("No item found for ({}).", locName);
        return 0;
    }
    return instance().m_locationItemInfo[locName].itemId;
}

int ArchipelagoContext::GetItemAtLocation(int locId) {
    for (const auto& [locName, locInfo] : instance().m_locationItemInfo) {
        if (locInfo.apLocationId == locId) {
            return locInfo.itemId;
        }
    }
    return 0;
}

void ArchipelagoContext::CreateArchipelagoWorld() {
    std::filesystem::path workingDir;
    GetSeedDirectoryPath(workingDir);

    auto trackerRando = randomizer::Randomizer(workingDir);
    trackerRando.GenerateTrackerWorld(false);

    instance().m_archiWorld = std::move(trackerRando.GetWorlds().front());
}

void ArchipelagoContext::FillArchipelagoWorld() {
    auto& world = instance().m_archiWorld;

    if (world == nullptr) {
        DuskLog.error("Archipelago world was not created!");
        return;
    }

    auto& locationInfo = instance().m_locationItemInfo;

    // fill all locations with data pulled from archi session
    for (auto location : world->GetAllLocations()) {
        // skip locations that aren't progression, which are locations that just aren't randomized
        if (!location->IsProgression()) {
            location->SetCurrentItem(location->GetOriginalItem());
            continue;
        }

        auto locName = location->GetName();
        if (!locationInfo.contains(locName)) {
            if (!location->HasCategories("Warp Portal") &&
                !location->HasCategories("Placeholder") &&
                !location->HasCategories("Hint Sign"))
                DuskLog.warn("Missing archipelago location data for: {}", locName);
            auto origItem = location->GetOriginalItem();

            // set location to original item

            if (origItem->GetID() != -1) // ensure item is not nothing
                location->SetCurrentItem(origItem);
            else
                DuskLog.info("Location ({}) does not have an original item!", locName);

            continue;
        }

        auto& locInfo = locationInfo[locName];
        if (locInfo.itemId != -1) {
            location->SetCurrentItem(world->GetItem(locInfo.itemId));
        }else {
            DuskLog.info("Skipping location ({}) as item is -1.", locName);
        }
    }
}

void ArchipelagoContext::CreateRandomizerContext() {
    auto& world = instance().m_archiWorld;

    // Set hint texts before writing context
    randomizer::logic::hints::GenerateAllHints(world);

    // TODO: generate archipelago item get text replacements

    auto randoData = WriteSeedData(world.get());
    randoData.mHash = GetArchipelagoSeedName();

    randomizer_GetContext() = randoData;

    std::filesystem::path workingDir;
    GetSeedDirectoryPath(workingDir);

    auto writeToFileResult = randoData.WriteToFile(workingDir / "seed.dat");

    if (writeToFileResult.has_value()) {
        DuskLog.error("Failed to create Rando Data. Reason: {}", writeToFileResult.value());
        return;
    }
}

void ArchipelagoContext::LoadRandomizerContext() {
    randomizer_GetContext() = RandomizerContext();

    std::filesystem::path workingDir;
    GetSeedDirectoryPath(workingDir);

    randomizer_GetContext().LoadFromPath(workingDir / "seed.dat");
    randomizer_GetContext().mHash = GetArchipelagoSeedName();
}

bool ArchipelagoContext::GenerateLocalWorldData() {
    if (instance().m_archiWorld != nullptr && !instance().m_seedName.empty()) {
        return true;
    }

    bool createContext = false;
    std::filesystem::path workingDir;

    GetSeedDirectoryPath(workingDir);

    bool hasSeedDat = std::filesystem::exists(workingDir / "seed.dat");

    if (hasSeedDat) {
        instance().m_config.LoadFromFile(workingDir / "settings.yaml", workingDir / "preferences.yaml");
    } else {
        std::filesystem::create_directories(workingDir);
        instance().m_config.LoadFromFile(workingDir / "settings.yaml", workingDir / "preferences.yaml");

        if (instance().m_SettingsFile.empty()) {
            DuskLog.error("[AP] Settings Data was not sent to client.");
            return false;
        }

        GenerateConfigFromAP(instance().m_config, instance().m_SettingsFile);

        instance().m_config.WriteToFile(workingDir / "settings.yaml", workingDir / "preferences.yaml");

        createContext = true;
    }

    CreateArchipelagoWorld();

    FillArchipelagoWorld();

    if (createContext) {
        CreateRandomizerContext();
    } else {
        try {
            LoadRandomizerContext();
        } catch (const std::exception& e) {
            DuskLog.error("[AP] Failed to load randomizer context: {}", e.what());
            return false;
        }
    }

    return true;
}
} // dusk::archi
