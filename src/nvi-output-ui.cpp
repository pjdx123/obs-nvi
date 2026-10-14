#include "nvi-output.h"

#include "nvi-plugin.h"

#include <obs-frontend-api.h>

#include <util/platform.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

/*
 * 工具菜单里的 NVI 输出对话框。
 *
 * 复用了 nvi-output.cpp 中 obs_output_info 的默认值与设置键，属性变更以 JSON
 * 的形式保存在模块配置目录（output.json），下次打开对话框时恢复。
 */

static QString nvi_tr(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* ------------------------------------------------------------------------- */
/* 设置持久化                                                                */
/* ------------------------------------------------------------------------- */

static std::string nvi_output_settings_file()
{
	char *path = obs_module_config_path("output.json");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

static obs_data_t *nvi_output_settings_create()
{
	obs_data_t *data = obs_data_create();

	/* 以输出的 get_defaults 为唯一默认值来源 */
	obs_output_info info = nvi_output_get_info();
	if (info.get_defaults)
		info.get_defaults(data);

	const std::string file = nvi_output_settings_file();
	if (!file.empty() && os_file_exists(file.c_str())) {
		obs_data_t *saved = obs_data_create_from_json_file_safe(file.c_str(), "bak");
		if (saved) {
			obs_data_apply(data, saved);
			obs_data_release(saved);
		}
	}

	return data;
}

static void nvi_output_settings_store(obs_data_t *data)
{
	const std::string file = nvi_output_settings_file();
	if (file.empty())
		return;

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	obs_data_save_json_safe(data, file.c_str(), "tmp", "bak");
}

/* ------------------------------------------------------------------------- */
/* 对话框                                                                    */
/* ------------------------------------------------------------------------- */

class NviOutputDialog : public QDialog {
	Q_OBJECT

public:
	explicit NviOutputDialog(QWidget *parent);

private slots:
	void toggleOutput();
	void refreshStatus();

private:
	void buildUi();
	void loadSettings();
	obs_data_t *currentSettings() const;

	QLineEdit *alias_ = nullptr;
	QComboBox *codec_ = nullptr;
	QLineEdit *local_ = nullptr;
	QLineEdit *tags_ = nullptr;
	QCheckBox *video_ = nullptr;
	QCheckBox *audio_ = nullptr;
	QPushButton *toggle_ = nullptr;
	QLabel *status_ = nullptr;
	QTimer *timer_ = nullptr;
};

NviOutputDialog::NviOutputDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(nvi_tr("NVIPlugin.OutputDialog.Title"));
	setMinimumWidth(420);

	buildUi();
	loadSettings();
	refreshStatus();

	timer_ = new QTimer(this);
	connect(timer_, &QTimer::timeout, this, &NviOutputDialog::refreshStatus);
	timer_->start(500);
}

void NviOutputDialog::buildUi()
{
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout();

	alias_ = new QLineEdit(this);
	form->addRow(nvi_tr("NVIPlugin.OutputProps.Alias"), alias_);

	codec_ = new QComboBox(this);
	codec_->addItem(nvi_tr("NVIPlugin.OutputProps.Codec.AVC"), (int)NVI_CODEC_AVC);
	codec_->addItem(nvi_tr("NVIPlugin.OutputProps.Codec.HEVC"), (int)NVI_CODEC_HEVC);
	codec_->addItem(nvi_tr("NVIPlugin.OutputProps.Codec.HF"), (int)NVI_CODEC_HF);
	codec_->addItem(nvi_tr("NVIPlugin.OutputProps.Codec.SUVC"), (int)NVI_CODEC_SUVC);
	form->addRow(nvi_tr("NVIPlugin.OutputProps.Codec"), codec_);

	local_ = new QLineEdit(this);
	form->addRow(nvi_tr("NVIPlugin.OutputProps.Local"), local_);

	tags_ = new QLineEdit(this);
	form->addRow(nvi_tr("NVIPlugin.OutputProps.Tags"), tags_);

	video_ = new QCheckBox(nvi_tr("NVIPlugin.OutputProps.Video"), this);
	form->addRow(QString(), video_);

	audio_ = new QCheckBox(nvi_tr("NVIPlugin.OutputProps.Audio"), this);
	form->addRow(QString(), audio_);

	layout->addLayout(form);

	auto *status_box = new QGroupBox(nvi_tr("NVIPlugin.OutputDialog.Status"), this);
	auto *status_layout = new QVBoxLayout(status_box);
	status_ = new QLabel(status_box);
	status_->setWordWrap(true);
	status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	status_layout->addWidget(status_);
	layout->addWidget(status_box);

	auto *buttons = new QDialogButtonBox(this);
	toggle_ = buttons->addButton(nvi_tr("NVIPlugin.OutputDialog.Start"), QDialogButtonBox::ActionRole);
	auto *close_button = buttons->addButton(QDialogButtonBox::Close);
	connect(toggle_, &QPushButton::clicked, this, &NviOutputDialog::toggleOutput);
	connect(close_button, &QPushButton::clicked, this, &QDialog::close);
	layout->addWidget(buttons);
}

void NviOutputDialog::loadSettings()
{
	obs_data_t *data = nvi_output_settings_create();

	alias_->setText(QString::fromUtf8(obs_data_get_string(data, NVI_OUT_ALIAS)));
	codec_->setCurrentIndex(codec_->findData((int)obs_data_get_int(data, NVI_OUT_CODEC)));
	local_->setText(QString::fromUtf8(obs_data_get_string(data, NVI_OUT_LOCAL)));
	tags_->setText(QString::fromUtf8(obs_data_get_string(data, NVI_OUT_TAGS)));
	video_->setChecked(obs_data_get_bool(data, NVI_OUT_VIDEO));
	audio_->setChecked(obs_data_get_bool(data, NVI_OUT_AUDIO));

	obs_data_release(data);
}

obs_data_t *NviOutputDialog::currentSettings() const
{
	obs_data_t *data = obs_data_create();

	obs_data_set_string(data, NVI_OUT_ALIAS, alias_->text().toUtf8().constData());
	obs_data_set_int(data, NVI_OUT_CODEC, codec_->currentData().toInt());
	obs_data_set_string(data, NVI_OUT_LOCAL, local_->text().toUtf8().constData());
	obs_data_set_string(data, NVI_OUT_TAGS, tags_->text().toUtf8().constData());
	obs_data_set_bool(data, NVI_OUT_VIDEO, video_->isChecked());
	obs_data_set_bool(data, NVI_OUT_AUDIO, audio_->isChecked());

	return data;
}

void NviOutputDialog::toggleOutput()
{
	if (nvi_output_is_running()) {
		nvi_output_stop_current();
	} else {
		obs_data_t *data = currentSettings();
		nvi_output_settings_store(data);
		nvi_output_start_with(data);
		obs_data_release(data);
	}

	refreshStatus();
}

void NviOutputDialog::refreshStatus()
{
	const NviOutputStatus status = nvi_output_query_status();
	const bool running = status.running;

	toggle_->setText(running ? nvi_tr("NVIPlugin.OutputDialog.Stop") : nvi_tr("NVIPlugin.OutputDialog.Start"));

	const bool editable = !running;
	alias_->setEnabled(editable);
	codec_->setEnabled(editable);
	local_->setEnabled(editable);
	tags_->setEnabled(editable);
	video_->setEnabled(editable);
	audio_->setEnabled(editable);

	QString text;
	if (running)
		text = nvi_tr("NVIPlugin.OutputDialog.Running").arg((qulonglong)status.pulled);
	else
		text = nvi_tr("NVIPlugin.OutputDialog.Stopped");

	if (!status.last_error.empty())
		text += QLatin1String("\n") + nvi_tr("NVIPlugin.OutputDialog.Error") + ": " +
			QString::fromStdString(status.last_error);

	status_->setText(text);
}

/* ------------------------------------------------------------------------- */
/* 注册                                                                      */
/* ------------------------------------------------------------------------- */

void nvi_output_settings_register()
{
	QWidget *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!main_window)
		return;

	auto *action = static_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction(obs_module_text("NVIPlugin.Menu.OutputSettings")));
	if (!action)
		return;

	/* 父对象为主窗口，随主窗口一起销毁 */
	auto *dialog = new NviOutputDialog(main_window);

	QObject::connect(action, &QAction::triggered, dialog, [dialog]() {
		dialog->show();
		dialog->raise();
		dialog->activateWindow();
	});
}

#include "nvi-output-ui.moc"
