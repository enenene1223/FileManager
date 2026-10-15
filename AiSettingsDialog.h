#pragma once
#include <QDialog>
#include "AiCredentialStore.h"
class QNetworkAccessManager;

class AiSettingsDialog : public QDialog {
public:
    explicit AiSettingsDialog(QWidget* parent = nullptr, AiCredentialStore store = AiCredentialStore(), QNetworkAccessManager* network = nullptr);
};
