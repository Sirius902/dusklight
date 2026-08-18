#include <dusk/ui/archi_connect_modal.hpp>
#include <dusk/ui/string_button.hpp>

#include "dusk/archipelago/archipelago_context.hpp"
#include "m_Do/m_Do_audio.h"

namespace dusk::ui {


void CreateSetupConnectionInfoModal();

ArchiConnectModal::ArchiConnectModal() :
    Modal({
       .title = "Archipelago",
       .bodyRml = "Connecting to Server...",
       .onDismiss = [this](Modal& modal) {
           auto phase = archi::ArchipelagoContext::GetConnectionPhase();
           if (phase == archi::ArchipelagoContext::ConnectionPhase::CONNECTING ||
               phase == archi::ArchipelagoContext::ConnectionPhase::SLOT_CONNECTED ||
               phase == archi::ArchipelagoContext::ConnectionPhase::GENERATING ||
               phase == archi::ArchipelagoContext::ConnectionPhase::ERROR ||
               phase == archi::ArchipelagoContext::ConnectionPhase::INVALID_SAVE) {
               archi::ArchipelagoContext::DisconnectFromServer();
           }
           mDoAud_seStartMenu(kSoundWindowClose);
           modal.pop(false);
       },
       .icon = "verifying",
    }) {
    mRoot->SetProperty("white-space", "pre-line");
}

void ArchiConnectModal::update() {
    Modal::update();

    if (mDisplayedStatus == ConnectionStatus::Ready) return;

    auto phase = archi::ArchipelagoContext::GetConnectionPhase();
    ConnectionStatus currentStatus;

    switch (phase) {
    case archi::ArchipelagoContext::ConnectionPhase::CONNECTING:
    case archi::ArchipelagoContext::ConnectionPhase::SLOT_CONNECTED:
        currentStatus = ConnectionStatus::Connecting;
        break;
    case archi::ArchipelagoContext::ConnectionPhase::GENERATING:
        currentStatus = ConnectionStatus::Generating;
        break;
    case archi::ArchipelagoContext::ConnectionPhase::CONNECTED:
        currentStatus = ConnectionStatus::Success;
        break;
    case archi::ArchipelagoContext::ConnectionPhase::ERROR:
        currentStatus = ConnectionStatus::Error;
        break;
    case archi::ArchipelagoContext::ConnectionPhase::INVALID_SAVE:
        currentStatus = ConnectionStatus::InvalidSave;
        break;
    default:
        return;
    }

    if (currentStatus == mDisplayedStatus) return;
    mDisplayedStatus = currentStatus;

    if (currentStatus == ConnectionStatus::Success) {
        mDoAud_seStartMenu(kSoundSeedGenerateSuccess);
        set_icon("celebration");
        set_body("Successfully Connected to server!");
        add_action({
            .label = "OK",
            .onPressed = [](Modal& modal) {
                mDoAud_seStartMenu(kSoundWindowClose);
                modal.pop(false);
            }
        });
        focus();
        mDisplayedStatus = ConnectionStatus::Ready;
    } else if (currentStatus == ConnectionStatus::Error) {
        mDoAud_seStartMenu(kSoundSeedGenerateError);
        set_icon("error");
        set_body("Failed to Connect to server.");
        add_action({
            .label = "Retry",
            .onPressed = [](Modal& modal) {
                mDoAud_seStartMenu(kSoundWindowClose);
                modal.pop(false);

                // show connection setup modal on failure
                CreateSetupConnectionInfoModal();
            }
        });
        add_action({
            .label = "Play Offline",
            .onPressed = [](Modal& modal) {
                // open the file with its locally stored seed data; checks
                // made offline are sent on the next connected session
                archi::ArchipelagoContext::RequestOfflinePlay();
                archi::ArchipelagoContext::DisconnectFromServer();
                mDoAud_seStartMenu(kSoundWindowClose);
                modal.pop(false);
            }
        });
        focus();
        mDisplayedStatus = ConnectionStatus::Ready;
    } else if (currentStatus == ConnectionStatus::InvalidSave) {
        mDoAud_seStartMenu(kSoundSeedGenerateError);
        set_icon("error");
        set_body("This save is not valid for this Archipelago.");
        add_action({
            .label = "OK",
            .onPressed = [](Modal& modal) {
                archi::ArchipelagoContext::DisconnectFromServer();
                mDoAud_seStartMenu(kSoundWindowClose);
                modal.pop(false);
            }
        });
        add_action({
            .label = "Play Offline",
            .onPressed = [](Modal& modal) {
                // the save is valid on its own; only this session's
                // seed/slot doesn't match it
                archi::ArchipelagoContext::RequestOfflinePlay();
                archi::ArchipelagoContext::DisconnectFromServer();
                mDoAud_seStartMenu(kSoundWindowClose);
                modal.pop(false);
            }
        });
        focus();
        mDisplayedStatus = ConnectionStatus::Ready;
    } else if (currentStatus == ConnectionStatus::Generating) {
        set_body("Loading seed data into game...");
    }
}

void ConnectAndLoadArchipelago() {
    if (archi::ArchipelagoContext::GetConnectionPhase() !=
        archi::ArchipelagoContext::ConnectionPhase::IDLE) {
        archi::ArchipelagoContext::DisconnectFromServer();
    }
    archi::ArchipelagoContext::ConnectToServer(dComIfGs_getDataNum());

    push_document(std::make_unique<ArchiConnectModal>());

    if (auto* doc = top_document()) {
        doc->focus();
    }
}

void CreateSetupConnectionInfoModal() {
    auto& doc = push_document(std::make_unique<MultiTextInputModal>(Modal::Props{
        .title = "Connection Info",
        .bodyRml = "",
        .actions = {
            ModalAction{
                .label = "Connect",
                .onPressed = [](Modal& modal) {
                    auto textModal = dynamic_cast<MultiTextInputModal*>(&modal);

                    int dataNum = dComIfGs_getDataNum();

                    archi::ArchipelagoContext::SetServerIp(textModal->get_input_text(0), dataNum);
                    archi::ArchipelagoContext::SetPassword(textModal->get_input_text(1), dataNum);
                    archi::ArchipelagoContext::SetSlotName(textModal->get_input_text(2), dataNum);

                    modal.pop(false);

                    ConnectAndLoadArchipelago();
                },
            },
            ModalAction{
                .label = "Cancel",
                .onPressed = [](Modal& modal) {
                    archi::ArchipelagoContext::DisconnectFromServer();
                    modal.pop(false);
                },
            },
        },
        .icon = "information"
    }));
    auto inputModal = dynamic_cast<MultiTextInputModal*>(&doc);

    int dataNum = dComIfGs_getDataNum();

    inputModal->add_input_text("Server IP", archi::ArchipelagoContext::GetServerIp(dataNum));
    inputModal->add_input_text("Password", archi::ArchipelagoContext::GetPassword(dataNum));
    inputModal->add_input_text("Slot Name", archi::ArchipelagoContext::GetSlotName(dataNum));
}

void BeginArchipelagoConnectionUI(bool forceChangeConnection) {
    // A stale offline request from an earlier flow must not turn a plain
    // cancel into an offline open
    archi::ArchipelagoContext::ClearOfflinePlayRequest();

    if (forceChangeConnection) {
        CreateSetupConnectionInfoModal();
        return;
    }

    int dataNum = dComIfGs_getDataNum();
    bool hasConnectInfo = (!archi::ArchipelagoContext::GetServerIp(dataNum).empty() && !archi::ArchipelagoContext::GetSlotName(dataNum).empty());

    if (hasConnectInfo) {
        ConnectAndLoadArchipelago();
    }else {
        CreateSetupConnectionInfoModal();
    }
}
}
