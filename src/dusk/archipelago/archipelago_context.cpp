#include <dusk/archipelago/archipelago_context.hpp>

#include <ctime>
#include <list>

#include "apuuid.hpp"
#include "defaultdatapackagestore.hpp"
#include "d/d_item.h"
#include "d/actor/d_a_alink.h"
#include "dusk/config.hpp"
#include "dusk/logging.h"
#include "dusk/randomizer/game/tools.h"
#include "dusk/randomizer/game/verify_item_functions.h"
#include "dusk/randomizer/generator/logic/hints.hpp"
#include "dusk/ui/rando_config.hpp"
#include "dusk/ui/ui.hpp"

namespace dusk::archi
{

static constexpr int ARCHI_ITEM_OFFSET = 2320000;

struct SettingsNameConvert {
    static constexpr std::string kDefaultYes = "On";
    static constexpr std::string kDefaultNo = "Off";

    std::string apName;
    std::string dusklightName;
    std::vector<std::pair<std::string, std::string>> optionsConvert;

    const std::string& tryGetOptionConvert(const std::string& option) const {
        if (optionsConvert.empty()) {
            if (option == "Yes")
                return kDefaultYes;
            if (option == "No")
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

void ArchipelagoContext::itemRecvImpl(int id, bool notify) {
    if (!m_apItemToGameItem.contains(id)) {
        DuskLog.warn("[AP] Got an invalid Item Id: {}", id);
        return;
    }

    m_isAllowUpdateLocations = true; // guards against triggering UpdateCheckedLocations

    auto& item = m_apItemToGameItem[id];

    if (notify && item.importance == randomizer::logic::item::Importance::MAJOR) {
        DuskLog.info("[AP] Adding Item: {}", item.itemName);
        g_randomizerState.addItemToEventQueue(verifyProgressiveItem(item.itemId));
    }else {
        DuskLog.info("[AP] Silently Adding Item: {}", item.itemName);
        execItemGet(item.itemId);
    }

    m_isAllowUpdateLocations = false;
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
            // Check if link is currently in a cutscene
            if (linkActor->checkEventRun())
                break;

            // Ensure that link is not currently in a message-based event.
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

void ArchipelagoContext::ConnectToServer(int file) {
    config::Save();

    instance().LoadTempItemInfo();
    instance().LoadTempLocationInfo();

    instance().m_slotName = GetSlotName(file);
    instance().m_password = GetPassword(file);

    auto uri = GetServerIp(file);
    if (uri.find("://") == std::string::npos) {
        uri = "ws://" + uri;
    }

    auto randoPath = ui::GetRandomizerPath();
    std::filesystem::create_directories(randoPath);
    auto uuid = ap_get_uuid((randoPath / "ap_uuid.dat").string(), uri);

    instance().m_dataPackageStore = std::make_unique<DefaultDataPackageStore>();
    instance().m_client = std::make_unique<APClient>(
        uuid, "Twilight Princess", uri, "", instance().m_dataPackageStore.get());

    auto& client = *instance().m_client;

    client.set_room_info_handler([]() {
        instance().m_seedName = instance().m_client->get_seed();
        instance().m_client->ConnectSlot(
            instance().m_slotName, instance().m_password, 0b111, {});
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

        // Reconnection detection
        if (instance().m_archiWorld != nullptr) {
            instance().m_isNeedResetInv = true;
            instance().m_itemIndex = 0;
            instance().m_receivedItemsQueue.clear();
            instance().m_locationItemInfo.clear();
        }

        instance().m_connectionPhase = ConnectionPhase::SLOT_CONNECTED;
        RequestAllLocationScout();
    });

    client.set_slot_refused_handler([](const std::list<std::string>& errors) {
        for (const auto& err : errors) {
            DuskLog.error("[AP] Connection refused: {}", err);
        }
        instance().m_connectionPhase = ConnectionPhase::ERROR;
    });

    client.set_socket_error_handler([](const std::string& error) {
        DuskLog.error("[AP] Socket error: {}", error);
    });

    client.set_socket_disconnected_handler([]() {
        DuskLog.info("[AP] Socket disconnected.");
        auto phase = instance().m_connectionPhase;
        if (phase == ConnectionPhase::CONNECTED ||
            phase == ConnectionPhase::GENERATING ||
            phase == ConnectionPhase::SLOT_CONNECTED) {
            instance().m_connectionPhase = ConnectionPhase::CONNECTING;
        } else if (phase != ConnectionPhase::ERROR) {
            instance().m_connectionPhase = ConnectionPhase::IDLE;
        }
    });

    client.set_items_received_handler([](const std::list<APClient::NetworkItem>& items) {
        auto& inst = instance();

        // Detect server-initiated reset
        if (!items.empty() && items.front().index == 0 && inst.m_itemIndex > 0) {
            inst.m_isNeedResetInv = true;
            inst.m_itemIndex = 0;
            inst.m_receivedItemsQueue.clear();
        }

        for (const auto& item : items) {
            if (static_cast<size_t>(item.index) < inst.m_itemIndex) continue;

            int relativeId = static_cast<int>(item.item - ARCHI_ITEM_OFFSET);
            bool notify = (inst.m_connectionPhase == ConnectionPhase::CONNECTED);

            // Rupee skip on replay
            if (!notify && ((relativeId >= 0 && relativeId <= 6) || relativeId == 7)) {
                inst.m_itemIndex = std::max(inst.m_itemIndex, static_cast<size_t>(item.index + 1));
                continue;
            }

            if (inst.m_connectionPhase != ConnectionPhase::CONNECTED) {
                inst.m_receivedItemsQueue.push_back({relativeId, notify});
            } else {
                if (!inst.m_isNeedResetInv && item.location != -1 &&
                    IsLocationChecked(item.location)) {
                    inst.m_itemIndex = std::max(inst.m_itemIndex, static_cast<size_t>(item.index + 1));
                    continue;
                }
                inst.itemRecvImpl(relativeId, notify);
            }

            inst.m_itemIndex = std::max(inst.m_itemIndex, static_cast<size_t>(item.index + 1));
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
            auto source = bounce["data"]["source"].get<std::string>();
            if (source == instance().m_client->get_slot()) return;
            DuskLog.info("Player {} sent death link.", source);
        }

        RequestPlayerDeath(true);
    });

    client.set_print_json_handler([](const APClient::PrintJSONArgs& args) {
        auto text = instance().m_client->render_json(args.data);

        if (args.type == "ItemSend") {
            ui::push_toast({
                .title = "Item Sent",
                .content = text,
                .duration = std::chrono::seconds(3),
            });
        } else if (args.type == "ItemRecv") {
            ui::push_toast({
                .title = "Item Received",
                .content = text,
                .duration = std::chrono::seconds(3),
            });
        }

        DuskLog.info("[AP] {}", text);
    });

    instance().m_connectionPhase = ConnectionPhase::CONNECTING;
}

void ArchipelagoContext::DisconnectFromServer() {
    instance().m_client.reset();
    instance().m_connectionPhase = ConnectionPhase::IDLE;
    instance().m_seedName.clear();
    instance().m_itemIndex = 0;
    instance().m_slot = -1;
    instance().m_receivedItemsQueue.clear();
    instance().m_locationItemInfo.clear();
    instance().m_initLocationCollectState.clear();
    instance().m_isEnableDeathLink = false;
    instance().m_isNeedResetInv = false;
}

bool ArchipelagoContext::IsConnected() {
    return instance().m_connectionPhase >= ConnectionPhase::SLOT_CONNECTED &&
           instance().m_connectionPhase != ConnectionPhase::ERROR;
}

void ArchipelagoContext::Poll() {
    auto& inst = instance();
    if (!inst.m_client) return;

    inst.m_client->poll();

    if (inst.m_connectionPhase == ConnectionPhase::GENERATING) {
        GenerateLocalWorldData();
        inst.m_connectionPhase = ConnectionPhase::CONNECTED;

        // Initial connection: drain items now (Execute() won't run yet)
        // Reconnection: DON'T drain — let Execute() reset inventory first
        if (!inst.m_isNeedResetInv) {
            for (auto& [id, notify] : inst.m_receivedItemsQueue)
                inst.itemRecvImpl(id, notify);
            inst.m_receivedItemsQueue.clear();
        }
    }
}

ArchipelagoContext::ConnectionPhase ArchipelagoContext::GetConnectionPhase() {
    return instance().m_connectionPhase;
}

void ArchipelagoContext::Execute() {
    if (instance().m_connectionPhase != ConnectionPhase::CONNECTED) return;

    if (instance().m_isNeedResetInv) {
        HandleResetInventory();
        instance().m_isNeedResetInv = false;
        return; // items drain next frame
    }

    // Drain reconnection items (after inventory was reset last frame)
    if (!instance().m_receivedItemsQueue.empty()) {
        for (auto& [id, notify] : instance().m_receivedItemsQueue)
            instance().itemRecvImpl(id, notify);
        instance().m_receivedItemsQueue.clear();
    }

    if (instance().tryKillPlayer()) return;

    if (instance().m_isUpdateLocations) {
        UpdateCheckedLocations();
        instance().m_isUpdateLocations = false;
    }
}

void ArchipelagoContext::HandleResetInventory() {
    DuskLog.info("Resetting Inventory.");
    // NOTE: this does not clear ALL things from save, so if a player managed to do something while disconnected from the archi, it might mess with things

    auto& playerInfo = g_dComIfG_gameInfo.info.getPlayer();

    // reset items
    playerInfo.getItem().init();
    playerInfo.getGetItem().init();

    // reset collect (poes, shards, swords)
    playerInfo.getCollect().init();

    playerInfo.getPlayerStatusA().setMaxLife(15);
    playerInfo.getPlayerStatusA().setWalletSize(WALLET);
    // dont reset rupees, and instead reject rupee updates while refilling inv

    // add back default items

    execItemGet(dItemNo_WEAR_KOKIRI_e);

    // sync all location collect flags with current collection status obtained from initial room connection
    UpdateAllLocationState();

    // clear all item-related flags

    dComIfGs_offEventBit(0x2580); // Power up dominion rod

    // shadow crystal
    dComIfGs_offEventBit(0xD04); // Can transform at will
    dComIfGs_offEventBit(0x501); // Midna Charge Unlocked

    // hidden skills
    dComIfGs_offEventBit(0x2904); // ENDING BLOW
    dComIfGs_offEventBit(0x2908); // SHIELD ATTACK
    dComIfGs_offEventBit(0x2902); // BACK SLICE
    dComIfGs_offEventBit(0x2901); // HELM SPLITTER
    dComIfGs_offEventBit(0x2A80); // MORTAL DRAW
    dComIfGs_offEventBit(0x2A40); // JUMP STRIKE
    dComIfGs_offEventBit(0x2A20); // GREAT SPIN

}

void ArchipelagoContext::UpdateCheckedLocations() {
    auto& world = instance().m_archiWorld;

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
        }
    }

    if (!batch.empty() && instance().m_client) {
        instance().m_client->LocationChecks(batch);
    } else if (batch.empty()) {
        DuskLog.warn("No locations had any changes! this might not be normal.");
    }
}

void ArchipelagoContext::SetNeedUpdateLocations(bool update) {
    if (!instance().m_isAllowUpdateLocations)
        instance().m_isUpdateLocations = update;
}

bool ArchipelagoContext::IsLocationChecked(int64_t locId) {
    auto& world = instance().m_archiWorld;

    for (const auto& [locName, locInfo] : instance().m_locationItemInfo) {
        if (locInfo.apLocationId == locId) {
            if (locInfo.collected)
                return true;

            if (auto location = world->GetLocation(locInfo.locationName, true)) {
                return isLocationObtained(location);
            }

            DuskLog.error("Failed to obtain location: {}", locName);
            return false;
        }
    }
    return false;
}

void ArchipelagoContext::SetLocationChecked(int64_t locId, bool collected) {
    // func was ran before location scouts could be sent out, cache result until scouts return.
    if (!IsReceivedLocationScouts()) {
        instance().m_initLocationCollectState[locId] = collected;
        return;
    }

    auto& world = instance().m_archiWorld;

    for (auto& [locName, locInfo] : instance().m_locationItemInfo) {
        if (locInfo.apLocationId == locId) {
            locInfo.collected = collected;

            // update location flags if possible
            auto location = world->GetLocation(locInfo.locationName, true);
            if (!location || !location->IsProgression())
                return;

            setLocationCollected(location, collected);
            return;
        }
    }

    DuskLog.warn("No location found for locId {}.", locId);
}

void ArchipelagoContext::UpdateLocationState(int64_t locId, bool collected) {
    auto& world = instance().m_archiWorld;

    for (const auto& [locName, locInfo] : instance().m_locationItemInfo) {
        if (locInfo.apLocationId == locId) {
            auto location = world->GetLocation(locInfo.locationName, true);
            if (!location || !location->IsProgression())
                continue;

            setLocationCollected(location, collected);
            return;
        }
    }

    DuskLog.warn("No location found for locId {}.", locId);
}

void ArchipelagoContext::UpdateAllLocationState() {
    auto& world = instance().m_archiWorld;
    // TODO: find out why some locations seem to keep their collection state upon reset (bugs)

    for (const auto& [locName, locInfo] : instance().m_locationItemInfo) {
        auto location = world->GetLocation(locInfo.locationName, true);
        if (!location || !location->IsProgression())
            continue;

        setLocationCollected(location, locInfo.collected);
    }
}

bool ArchipelagoContext::IsReceivedLocationScouts() {
    return !instance().m_locationItemInfo.empty();
}

void ArchipelagoContext::TryHandleDeathLink() {
    if (!instance().m_client) return;
    if (instance().m_isEnableDeathLink && !instance().m_isFromDeathLink) {
        nlohmann::json deathData = {
            {"time", std::time(nullptr)},
            {"cause", fmt::format("{} was unable to become the Hero of Twilight.",
                                  instance().m_client->get_slot())},
            {"source", instance().m_client->get_slot()}
        };
        instance().m_client->Bounce(deathData, {}, {}, {"DeathLink"});
    }
}

bool ArchipelagoContext::TryHandleGameComplete() {
    if (!instance().m_client) return false;
    instance().m_client->StatusUpdate(APClient::ClientStatus::GOAL);
    return true;
}

void ArchipelagoContext::RequestAllLocationScout(bool isHint) {
    std::list<int64_t> locations;
    for (int i = 0; i < 475; i++) {
        locations.push_back(ARCHI_ITEM_OFFSET + i);
    }

    instance().m_client->LocationScouts(locations, isHint ? 1 : 0);
}

void ArchipelagoContext::RequestPlayerDeath(bool isDeathLink) {
    instance().m_isNeedPlayerDeath = true;
    instance().m_isFromDeathLink = isDeathLink;
}

bool ArchipelagoContext::GenerateConfigFromAP(randomizer::seedgen::config::Config& config, const std::string& settingsStr) {
    YAML::Node apConfigYaml;
    try {
        apConfigYaml = YAML::Load(settingsStr);
    }catch (YAML::BadFile& e) {
        DuskLog.warn("Failed to load AP Config YAML file!");
        return false;
    }

    config.SetSeed("Archipelago");
    randomizer::seedgen::settings::Settings& settings = config.GetSettings();

    // update settings using ap config
    for (const auto& apSettingEntry : apConfigYaml) {
        auto apSettingName = apSettingEntry.first.as<std::string>();
        auto apSettingValue = apSettingEntry.second.as<std::string>();

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
        }else {
            DuskLog.debug("Missing Setting: {} Value: {}", apSettingName, apSettingValue);
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

void ArchipelagoContext::GenerateLocalWorldData() {
    bool createContext = false;
    std::filesystem::path workingDir;

    GetSeedDirectoryPath(workingDir);

    if (std::filesystem::exists(workingDir)) {
        instance().m_config.LoadFromFile(workingDir / "settings.yaml", workingDir / "preferences.yaml");
    }else {
        std::filesystem::create_directories(workingDir);
        // creates base yamls at directory if they dont exist yet
        instance().m_config.LoadFromFile(workingDir / "settings.yaml", workingDir / "preferences.yaml");

        if (instance().m_SettingsFile.empty()) {
            DuskLog.fatal("Settings Data was not sent to client! Unable to generate world data.");
            return;
        }

        GenerateConfigFromAP(instance().m_config, instance().m_SettingsFile);

        instance().m_config.WriteToFile(workingDir / "settings.yaml", workingDir / "preferences.yaml");

        createContext = true;
    }

    CreateArchipelagoWorld();

    FillArchipelagoWorld();

    if (createContext) {
        CreateRandomizerContext();
    }else {
        LoadRandomizerContext();
    }
}
} // dusk::archi
