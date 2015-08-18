#include <string>

#include "templatelayout.h"
#include "helpers.h"
#include "display.h"

QList<QString> grantlee_templates;

int getTotalWork(print_options *printOptions)
{
	if (printOptions->print_selected) {
		// return the correct number depending on all/selected dives
		// but don't return 0 as we might divide by this number
		return amount_selected ? amount_selected : 1;
	}
	int dives = 0, i;
	struct dive *dive;
	for_each_dive (i, dive) {
		dives++;
	}
	return dives;
}

void find_all_templates()
{
	grantlee_templates.clear();
	QDir dir(getSubsurfaceDataPath("printing_templates"));
	QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
	foreach (QFileInfo finfo, list) {
		QString filename = finfo.fileName();
		if (filename.at(filename.size() - 1) != '~') {
			grantlee_templates.append(finfo.fileName());
		}
	}
}

TemplateLayout::TemplateLayout(print_options *PrintOptions, template_options *templateOptions) :
	m_engine(NULL)
{
	this->PrintOptions = PrintOptions;
	this->templateOptions = templateOptions;
}

TemplateLayout::~TemplateLayout()
{
	delete m_engine;
}

QString TemplateLayout::generate()
{
	int progress = 0;
	int totalWork = getTotalWork(PrintOptions);

	QString htmlContent;
	m_engine = new Grantlee::Engine(this);

	QSharedPointer<Grantlee::FileSystemTemplateLoader> m_templateLoader =
		QSharedPointer<Grantlee::FileSystemTemplateLoader>(new Grantlee::FileSystemTemplateLoader());
	m_templateLoader->setTemplateDirs(QStringList() << getSubsurfaceDataPath("printing_templates"));
	m_engine->addTemplateLoader(m_templateLoader);

	Grantlee::registerMetaType<Dive>();
	Grantlee::registerMetaType<template_options>();
	Grantlee::registerMetaType<print_options>();

	QVariantHash mapping;
	QVariantList diveList;

	struct dive *dive;
	int i;
	for_each_dive (i, dive) {
		//TODO check for exporting selected dives only
		if (!dive->selected && PrintOptions->print_selected)
			continue;
		Dive d(dive);
		diveList.append(QVariant::fromValue(d));
		progress++;
		emit progressUpdated(progress * 100.0 / totalWork);
	}
	mapping.insert("dives", diveList);
	mapping.insert("template_options", QVariant::fromValue(*templateOptions));
	mapping.insert("print_options", QVariant::fromValue(*PrintOptions));

	Grantlee::Context c(mapping);

	Grantlee::Template t = m_engine->loadByName(PrintOptions->p_template);
	if (!t || t->error()) {
		qDebug() << "Can't load template";
		return htmlContent;
	}

	htmlContent = t->render(&c);

	if (t->error()) {
		qDebug() << "Can't render template";
		return htmlContent;
	}
	return htmlContent;
}

QString TemplateLayout::readTemplate(QString template_name)
{
	QFile qfile(getSubsurfaceDataPath("printing_templates") + QDir::separator() + template_name);
	if (qfile.open(QFile::ReadOnly | QFile::Text)) {
		QTextStream in(&qfile);
		return in.readAll();
	}
	return "";
}

void TemplateLayout::writeTemplate(QString template_name, QString grantlee_template)
{
	QFile qfile(getSubsurfaceDataPath("printing_templates") + QDir::separator() + template_name);
	if (qfile.open(QFile::ReadWrite | QFile::Text)) {
		qfile.write(grantlee_template.toUtf8().data());
		qfile.resize(qfile.pos());
		qfile.close();
	}
}
