#pragma once
#include "modal.hpp"

namespace dusk::ui
{


    class ArchiConnectModal : public Modal {
    public:
        enum class ConnectionStatus {
            None,
            Ready,
            Connecting,
            Generating,
            Success,
            Error,
            InvalidSave,
        };

        explicit ArchiConnectModal();
        void update() override;

    private:
        ConnectionStatus mDisplayedStatus = ConnectionStatus::None;
    };

    void BeginArchipelagoConnectionUI(bool forceChangeConnection = false);

}