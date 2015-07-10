#include "qmlmanager.h"
#include <QUrl>
#include <QSettings>

#include "qt-models/divelistmodel.h"
#include "divelist.h"
#include "pref.h"
#include "qthelper.h"

QMLManager::QMLManager()
{
	//Initialize cloud credentials.
	setCloudUserName(prefs.cloud_storage_email);
	setCloudPassword(prefs.cloud_storage_password);
}

QMLManager::~QMLManager()
{
}

void QMLManager::savePreferences()
{
	QSettings s;
	s.beginGroup("CloudStorage");
	s.setValue("email", cloudUserName());
	s.setValue("password", cloudPassword());

	s.sync();
}

void QMLManager::loadDives()
{
	QString url;
	if (getCloudURL(url)) {
		//TODO: Show error in QML
		return;
	}

	QByteArray fileNamePrt  = QFile::encodeName(url);
	int error = parse_file(fileNamePrt.data());
	if (!error) {
		set_filename(fileNamePrt.data(), true);
	}

	process_dives(false, false);
	int i;
	struct dive *d;

	for_each_dive(i, d)
			DiveListModel::instance()->addDive(d);
}

void QMLManager::commitChanges(QString diveID, QString airtemp, QString watertemp, QString weight, QString suit, QString cylinder, QString notes, QString buddy, QString divemaster)
{
	struct dive *d = get_dive_by_uniq_id(diveID.toInt());
	d->buddy = strdup(buddy.toUtf8().data());
	d->suit = strdup(suit.toUtf8().data());
	d->notes = strdup(notes.toUtf8().data());
	d->divemaster = strdup(divemaster.toUtf8().data());
}

void QMLManager::saveToCloud()
{
	mark_divelist_changed(true);

	QString url;
	if (getCloudURL(url)) {
		//TODO: Show error in QML
		return;
	}

	QByteArray fileNamePtr = QFile::encodeName(url);
	save_dives(fileNamePtr.data());
}

QString QMLManager::cloudPassword() const
{
	return m_cloudPassword;
}

void QMLManager::setCloudPassword(const QString &cloudPassword)
{
	m_cloudPassword = cloudPassword;
	emit cloudPasswordChanged();
}

QString QMLManager::cloudUserName() const
{
	return m_cloudUserName;
}

void QMLManager::setCloudUserName(const QString &cloudUserName)
{
	m_cloudUserName = cloudUserName;
	emit cloudUserNameChanged();
}
