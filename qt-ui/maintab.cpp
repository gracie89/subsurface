/*
 * maintab.cpp
 *
 * classes for the "notebook" area of the main window of Subsurface
 *
 */
#include "maintab.h"
#include "mainwindow.h"
#include "../helpers.h"
#include "../statistics.h"
#include "divelistview.h"
#include "modeldelegates.h"
#include "globe.h"
#include "diveplanner.h"
#include "divelist.h"
#include "qthelper.h"
#include "display.h"
#include "divepicturewidget.h"

#include <QLabel>
#include <QCompleter>
#include <QDebug>
#include <QSet>
#include <QSettings>
#include <QTableView>
#include <QPalette>
#include <QScrollBar>
#include <QShortcut>
#include <QMessageBox>
#include <QDesktopServices>

MainTab::MainTab(QWidget *parent) : QTabWidget(parent),
	weightModel(new WeightModel(this)),
	cylindersModel(CylindersModel::instance()),
	editMode(NONE),
	divePictureModel(DivePictureModel::instance())
{
	ui.setupUi(this);

	memset(&displayed_dive, 0, sizeof(displayed_dive));

	ui.cylinders->setModel(cylindersModel);
	ui.weights->setModel(weightModel);
	ui.photosView->setModel(divePictureModel);
	connect(ui.photosView, SIGNAL(photoDoubleClicked(QString)), this, SLOT(photoDoubleClicked(QString)));
	closeMessage();

	QAction *action = new QAction(tr("Save"), this);
	connect(action, SIGNAL(triggered(bool)), this, SLOT(acceptChanges()));
	addMessageAction(action);

	action = new QAction(tr("Cancel"), this);
	connect(action, SIGNAL(triggered(bool)), this, SLOT(rejectChanges()));

	QShortcut *closeKey = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	connect(closeKey, SIGNAL(activated()), this, SLOT(escDetected()));

	addMessageAction(action);

	if (qApp->style()->objectName() == "oxygen")
		setDocumentMode(true);
	else
		setDocumentMode(false);

	// we start out with the fields read-only; once things are
	// filled from a dive, they are made writeable
	setEnabled(false);

	ui.location->installEventFilter(this);
	ui.coordinates->installEventFilter(this);
	ui.divemaster->installEventFilter(this);
	ui.buddy->installEventFilter(this);
	ui.suit->installEventFilter(this);
	ui.notes->viewport()->installEventFilter(this);
	ui.rating->installEventFilter(this);
	ui.visibility->installEventFilter(this);
	ui.airtemp->installEventFilter(this);
	ui.watertemp->installEventFilter(this);
	ui.dateEdit->installEventFilter(this);
	ui.timeEdit->installEventFilter(this);
	ui.tagWidget->installEventFilter(this);

	QList<QObject *> statisticsTabWidgets = ui.statisticsTab->children();
	Q_FOREACH (QObject *obj, statisticsTabWidgets) {
		QLabel *label = qobject_cast<QLabel *>(obj);
		if (label)
			label->setAlignment(Qt::AlignHCenter);
	}
	ui.cylinders->setTitle(tr("Cylinders"));
	ui.cylinders->setBtnToolTip(tr("Add Cylinder"));
	connect(ui.cylinders, SIGNAL(addButtonClicked()), this, SLOT(addCylinder_clicked()));

	ui.weights->setTitle(tr("Weights"));
	ui.weights->setBtnToolTip(tr("Add Weight System"));
	connect(ui.weights, SIGNAL(addButtonClicked()), this, SLOT(addWeight_clicked()));

	connect(ui.cylinders->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editCylinderWidget(QModelIndex)));
	connect(ui.weights->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editWeightWidget(QModelIndex)));

	ui.cylinders->view()->setItemDelegateForColumn(CylindersModel::TYPE, new TankInfoDelegate(this));
	ui.weights->view()->setItemDelegateForColumn(WeightModel::TYPE, new WSInfoDelegate(this));
	ui.cylinders->view()->setColumnHidden(CylindersModel::DEPTH, true);
	completers.buddy = new QCompleter(&buddyModel, ui.buddy);
	completers.divemaster = new QCompleter(&diveMasterModel, ui.divemaster);
	completers.location = new QCompleter(&locationModel, ui.location);
	completers.suit = new QCompleter(&suitModel, ui.suit);
	completers.tags = new QCompleter(&tagModel, ui.tagWidget);
	completers.buddy->setCaseSensitivity(Qt::CaseInsensitive);
	completers.divemaster->setCaseSensitivity(Qt::CaseInsensitive);
	completers.location->setCaseSensitivity(Qt::CaseInsensitive);
	completers.suit->setCaseSensitivity(Qt::CaseInsensitive);
	completers.tags->setCaseSensitivity(Qt::CaseInsensitive);
	ui.buddy->setCompleter(completers.buddy);
	ui.divemaster->setCompleter(completers.divemaster);
	ui.location->setCompleter(completers.location);
	ui.suit->setCompleter(completers.suit);
	ui.tagWidget->setCompleter(completers.tags);

	setMinimumHeight(0);
	setMinimumWidth(0);

	// Current display of things on Gnome3 looks like shit, so
	// let`s fix that.
	if (isGnome3Session()) {
		QPalette p;
		p.setColor(QPalette::Window, QColor(Qt::white));
		ui.scrollArea->viewport()->setPalette(p);
		ui.scrollArea_2->viewport()->setPalette(p);
		ui.scrollArea_3->viewport()->setPalette(p);
		ui.scrollArea_4->viewport()->setPalette(p);

		// GroupBoxes in Gnome3 looks like I'v drawn them...
		static const QString gnomeCss(
			"QGroupBox {"
			"    background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
			"    stop: 0 #E0E0E0, stop: 1 #FFFFFF);"
			"    border: 2px solid gray;"
			"    border-radius: 5px;"
			"    margin-top: 1ex;"
			"}"
			"QGroupBox::title {"
			"    subcontrol-origin: margin;"
			"    subcontrol-position: top center;"
			"    padding: 0 3px;"
			"    background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
			"    stop: 0 #E0E0E0, stop: 1 #FFFFFF);"
			"}");
		Q_FOREACH (QGroupBox *box, findChildren<QGroupBox *>()) {
			box->setStyleSheet(gnomeCss);
		}
	}
	ui.cylinders->view()->horizontalHeader()->setContextMenuPolicy(Qt::ActionsContextMenu);

	QSettings s;
	s.beginGroup("cylinders_dialog");
	for (int i = 0; i < CylindersModel::COLUMNS; i++) {
		if ((i == CylindersModel::REMOVE) || (i == CylindersModel::TYPE))
			continue;
		bool checked = s.value(QString("column%1_hidden").arg(i)).toBool();
		action = new QAction(cylindersModel->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString(), ui.cylinders->view());
		action->setCheckable(true);
		action->setData(i);
		action->setChecked(!checked);
		connect(action, SIGNAL(triggered(bool)), this, SLOT(toggleTriggeredColumn()));
		ui.cylinders->view()->setColumnHidden(i, checked);
		ui.cylinders->view()->horizontalHeader()->addAction(action);
	}
}

MainTab::~MainTab()
{
	QSettings s;
	s.beginGroup("cylinders_dialog");
	for (int i = 0; i < CylindersModel::COLUMNS; i++) {
		if ((i == CylindersModel::REMOVE) || (i == CylindersModel::TYPE))
			continue;
		s.setValue(QString("column%1_hidden").arg(i), ui.cylinders->view()->isColumnHidden(i));
	}
}

void MainTab::toggleTriggeredColumn()
{
	QAction *action = qobject_cast<QAction *>(sender());
	int col = action->data().toInt();
	QTableView *view = ui.cylinders->view();

	if (action->isChecked()) {
		view->showColumn(col);
		if (view->columnWidth(col) <= 15)
			view->setColumnWidth(col, 80);
	} else
		view->hideColumn(col);
}

void MainTab::addDiveStarted()
{
	enableEdition(ADD);
}

void MainTab::addMessageAction(QAction *action)
{
	ui.diveEquipmentMessage->addAction(action);
	ui.diveNotesMessage->addAction(action);
	ui.diveInfoMessage->addAction(action);
	ui.diveStatisticsMessage->addAction(action);
}

void MainTab::hideMessage()
{
	ui.diveNotesMessage->animatedHide();
	ui.diveEquipmentMessage->animatedHide();
	ui.diveInfoMessage->animatedHide();
	ui.diveStatisticsMessage->animatedHide();
	updateTextLabels(false);
}

void MainTab::closeMessage()
{
	hideMessage();
	ui.diveNotesMessage->setCloseButtonVisible(false);
	ui.diveEquipmentMessage->setCloseButtonVisible(false);
	ui.diveInfoMessage->setCloseButtonVisible(false);
	ui.diveStatisticsMessage->setCloseButtonVisible(false);
}

void MainTab::displayMessage(QString str)
{
	ui.diveNotesMessage->setText(str);
	ui.diveNotesMessage->animatedShow();
	ui.diveEquipmentMessage->setText(str);
	ui.diveEquipmentMessage->animatedShow();
	ui.diveInfoMessage->setText(str);
	ui.diveInfoMessage->animatedShow();
	ui.diveStatisticsMessage->setText(str);
	ui.diveStatisticsMessage->animatedShow();
	updateTextLabels();
}

void MainTab::updateTextLabels(bool showUnits)
{
	if (showUnits) {
		ui.airTempLabel->setText(tr("Air temp [%1]").arg(get_temp_unit()));
		ui.waterTempLabel->setText(tr("Water temp [%1]").arg(get_temp_unit()));
	} else {
		ui.airTempLabel->setText(tr("Air temp"));
		ui.waterTempLabel->setText(tr("Water temp"));
	}
}

void MainTab::enableEdition(EditMode newEditMode)
{
	if (current_dive == NULL || editMode != NONE)
		return;
	modified = false;
	if ((newEditMode == DIVE || newEditMode == NONE) &&
	    current_dive->dc.model &&
	    strcmp(current_dive->dc.model, "manually added dive") == 0) {
		// editCurrentDive will call enableEdition with newEditMode == MANUALLY_ADDED_DIVE
		// so exit this function here after editCurrentDive() returns
		MainWindow::instance()->editCurrentDive();
		return;
	}
	MainWindow::instance()->dive_list()->setEnabled(false);
	if (amount_selected == 1)
		MainWindow::instance()->globe()->prepareForGetDiveCoordinates();
	if (MainWindow::instance() && MainWindow::instance()->dive_list()->selectedTrips().count() == 1) {
		// we are editing trip location and notes
		displayMessage(tr("This trip is being edited."));
		displayed_dive.location = copy_string(current_dive->divetrip->location);
		displayed_dive.notes = copy_string(current_dive->divetrip->notes);
		ui.dateEdit->setEnabled(false);
		editMode = TRIP;
	} else {
		if (amount_selected > 1) {
			displayMessage(tr("Multiple dives are being edited."));
		} else {
			displayMessage(tr("This dive is being edited."));
		}
		// editedDive already contains the current dive (we set this up in updateDiveInfo),
		// so all we need to do is update the editMode if necessary
		editMode = newEditMode != NONE ? newEditMode : DIVE;
	}
}

void MainTab::clearEquipment()
{
	cylindersModel->clear();
	weightModel->clear();
}

void MainTab::nextInputField(QKeyEvent *event)
{
	keyPressEvent(event);
}

void MainTab::clearInfo()
{
	ui.sacText->clear();
	ui.otuText->clear();
	ui.oxygenHeliumText->clear();
	ui.gasUsedText->clear();
	ui.dateText->clear();
	ui.diveTimeText->clear();
	ui.surfaceIntervalText->clear();
	ui.maximumDepthText->clear();
	ui.averageDepthText->clear();
	ui.waterTemperatureText->clear();
	ui.airTemperatureText->clear();
	ui.airPressureText->clear();
	ui.salinityText->clear();
	ui.tagWidget->clear();
}

void MainTab::clearStats()
{
	ui.depthLimits->clear();
	ui.sacLimits->clear();
	ui.divesAllText->clear();
	ui.tempLimits->clear();
	ui.totalTimeAllText->clear();
	ui.timeLimits->clear();
}

#define UPDATE_TEXT(d, field)          \
	if (clear || !d.field)         \
		ui.field->setText(""); \
	else                           \
		ui.field->setText(d.field)

#define UPDATE_TEMP(d, field)            \
	if (clear || d.field.mkelvin == 0) \
		ui.field->setText("");   \
	else                             \
		ui.field->setText(get_temperature_string(d.field, true))

bool MainTab::isEditing()
{
	return editMode != NONE;
}

void MainTab::updateDiveInfo(bool clear)
{
	// don't execute this while adding / planning a dive
	if (editMode == ADD || editMode == MANUALLY_ADDED_DIVE || MainWindow::instance()->graphics()->isPlanner())
		return;
	if (!isEnabled() && !clear)
		setEnabled(true);
	if (isEnabled() && clear)
		setEnabled(false);
	editMode = IGNORE; // don't trigger on changes to the widgets

	// This method updates ALL tabs whenever a new dive or trip is
	// selected.
	// If exactly one trip has been selected, we show the location / notes
	// for the trip in the Info tab, otherwise we show the info of the
	// selected_dive
	temperature_t temp;
	struct dive *prevd;
	char buf[1024];

	process_selected_dives();
	process_all_dives(&displayed_dive, &prevd);

	divePictureModel->updateDivePictures();
	UPDATE_TEXT(displayed_dive, notes);
	UPDATE_TEXT(displayed_dive, location);
	UPDATE_TEXT(displayed_dive, suit);
	UPDATE_TEXT(displayed_dive, divemaster);
	UPDATE_TEXT(displayed_dive, buddy);
	UPDATE_TEMP(displayed_dive, airtemp);
	UPDATE_TEMP(displayed_dive, watertemp);

	if (!clear) {
		updateGpsCoordinates(&displayed_dive);
		QDateTime localTime = QDateTime::fromTime_t(displayed_dive.when - gettimezoneoffset());
		ui.dateEdit->setDate(localTime.date());
		ui.timeEdit->setTime(localTime.time());
		if (MainWindow::instance() && MainWindow::instance()->dive_list()->selectedTrips().count() == 1) {
			setTabText(0, tr("Trip Notes"));
			// only use trip relevant fields
			ui.coordinates->setVisible(false);
			ui.CoordinatedLabel->setVisible(false);
			ui.divemaster->setVisible(false);
			ui.DivemasterLabel->setVisible(false);
			ui.buddy->setVisible(false);
			ui.BuddyLabel->setVisible(false);
			ui.suit->setVisible(false);
			ui.SuitLabel->setVisible(false);
			ui.rating->setVisible(false);
			ui.RatingLabel->setVisible(false);
			ui.visibility->setVisible(false);
			ui.visibilityLabel->setVisible(false);
			ui.tagWidget->setVisible(false);
			ui.TagLabel->setVisible(false);
			ui.airTempLabel->setVisible(false);
			ui.airtemp->setVisible(false);
			ui.waterTempLabel->setVisible(false);
			ui.watertemp->setVisible(false);
			// rename the remaining fields and fill data from selected trip
			dive_trip_t *currentTrip = *MainWindow::instance()->dive_list()->selectedTrips().begin();
			ui.LocationLabel->setText(tr("Trip Location"));
			ui.location->setText(currentTrip->location);
			ui.NotesLabel->setText(tr("Trip Notes"));
			ui.notes->setText(currentTrip->notes);
			clearEquipment();
			ui.equipmentTab->setEnabled(false);
		} else {
			setTabText(0, tr("Dive Notes"));
			// make all the fields visible writeable
			ui.coordinates->setVisible(true);
			ui.CoordinatedLabel->setVisible(true);
			ui.divemaster->setVisible(true);
			ui.buddy->setVisible(true);
			ui.suit->setVisible(true);
			ui.SuitLabel->setVisible(true);
			ui.rating->setVisible(true);
			ui.RatingLabel->setVisible(true);
			ui.visibility->setVisible(true);
			ui.visibilityLabel->setVisible(true);
			ui.BuddyLabel->setVisible(true);
			ui.DivemasterLabel->setVisible(true);
			ui.TagLabel->setVisible(true);
			ui.tagWidget->setVisible(true);
			ui.airTempLabel->setVisible(true);
			ui.airtemp->setVisible(true);
			ui.waterTempLabel->setVisible(true);
			ui.watertemp->setVisible(true);
			/* and fill them from the dive */
			ui.rating->setCurrentStars(displayed_dive.rating);
			ui.visibility->setCurrentStars(displayed_dive.visibility);
			// reset labels in case we last displayed trip notes
			ui.LocationLabel->setText(tr("Location"));
			ui.NotesLabel->setText(tr("Notes"));
			ui.equipmentTab->setEnabled(true);
			cylindersModel->setDive(&displayed_dive);
			weightModel->setDive(&displayed_dive);
			taglist_get_tagstring(displayed_dive.tag_list, buf, 1024);
			ui.tagWidget->setText(QString(buf));
		}
		ui.maximumDepthText->setText(get_depth_string(displayed_dive.maxdepth, true));
		ui.averageDepthText->setText(get_depth_string(displayed_dive.meandepth, true));
		ui.otuText->setText(QString("%1").arg(displayed_dive.otu));
		ui.waterTemperatureText->setText(get_temperature_string(displayed_dive.watertemp, true));
		ui.airTemperatureText->setText(get_temperature_string(displayed_dive.airtemp, true));
		volume_t gases[MAX_CYLINDERS] = {};
		get_gas_used(&displayed_dive, gases);
		QString volumes = get_volume_string(gases[0], true);
		int mean[MAX_CYLINDERS], duration[MAX_CYLINDERS];
		per_cylinder_mean_depth(&displayed_dive, select_dc(&displayed_dive), mean, duration);
		volume_t sac;
		QString SACs;
		if (mean[0] && duration[0]) {
			sac.mliter = gases[0].mliter / (depth_to_atm(mean[0], &displayed_dive) * duration[0] / 60.0);
			SACs = get_volume_string(sac, true).append(tr("/min"));
		} else {
			SACs = QString(tr("unknown"));
		}
		for (int i = 1; i < MAX_CYLINDERS && gases[i].mliter != 0; i++) {
			volumes.append("\n" + get_volume_string(gases[i], true));
			if (duration[i]) {
				sac.mliter = gases[i].mliter / (depth_to_atm(mean[i], &displayed_dive) * duration[i] / 60);
				SACs.append("\n" + get_volume_string(sac, true).append(tr("/min")));
			} else {
				SACs.append("\n");
			}
		}
		ui.gasUsedText->setText(volumes);
		ui.oxygenHeliumText->setText(get_gaslist(&displayed_dive));
		ui.dateText->setText(get_short_dive_date_string(displayed_dive.when));
		ui.diveTimeText->setText(QString::number((int)((displayed_dive.duration.seconds + 30) / 60)));
		if (prevd)
			ui.surfaceIntervalText->setText(get_time_string(displayed_dive.when - (prevd->when + prevd->duration.seconds), 4));
		else
			ui.surfaceIntervalText->clear();
		if (mean[0])
			ui.sacText->setText(SACs);
		else
			ui.sacText->clear();
		if (displayed_dive.surface_pressure.mbar)
			/* this is ALWAYS displayed in mbar */
			ui.airPressureText->setText(QString("%1mbar").arg(displayed_dive.surface_pressure.mbar));
		else
			ui.airPressureText->clear();
		if (displayed_dive.salinity)
			ui.salinityText->setText(QString("%1g/l").arg(displayed_dive.salinity / 10.0));
		else
			ui.salinityText->clear();
		ui.depthLimits->setMaximum(get_depth_string(stats_selection.max_depth, true));
		ui.depthLimits->setMinimum(get_depth_string(stats_selection.min_depth, true));
		// the overall average depth is really confusing when listed between the
		// deepest and shallowest dive - let's just not set it
		// ui.depthLimits->setAverage(get_depth_string(stats_selection.avg_depth, true));
		ui.depthLimits->overrideMaxToolTipText(tr("Deepest Dive"));
		ui.depthLimits->overrideMinToolTipText(tr("Shallowest Dive"));
		ui.sacLimits->setMaximum(get_volume_string(stats_selection.max_sac, true).append(tr("/min")));
		ui.sacLimits->setMinimum(get_volume_string(stats_selection.min_sac, true).append(tr("/min")));
		ui.sacLimits->setAverage(get_volume_string(stats_selection.avg_sac, true).append(tr("/min")));
		ui.divesAllText->setText(QString::number(stats_selection.selection_size));
		temp.mkelvin = stats_selection.max_temp;
		ui.tempLimits->setMaximum(get_temperature_string(temp, true));
		temp.mkelvin = stats_selection.min_temp;
		ui.tempLimits->setMinimum(get_temperature_string(temp, true));
		if (stats_selection.combined_temp && stats_selection.combined_count) {
			const char *unit;
			get_temp_units(0, &unit);
			ui.tempLimits->setAverage(QString("%1%2").arg(stats_selection.combined_temp / stats_selection.combined_count, 0, 'f', 1).arg(unit));
		}
		ui.totalTimeAllText->setText(get_time_string(stats_selection.total_time.seconds, 0));
		int seconds = stats_selection.total_time.seconds;
		if (stats_selection.selection_size)
			seconds /= stats_selection.selection_size;
		ui.timeLimits->setAverage(get_time_string(seconds, 0));
		ui.timeLimits->setMaximum(get_time_string(stats_selection.longest_time.seconds, 0));
		ui.timeLimits->setMinimum(get_time_string(stats_selection.shortest_time.seconds, 0));
		// now let's get some gas use statistics
		QVector<QPair<QString, int> > gasUsed;
		QString gasUsedString;
		volume_t vol;
		selectedDivesGasUsed(gasUsed);
		for (int j = 0; j < 20; j++) {
			if (gasUsed.isEmpty())
				break;
			QPair<QString, int> gasPair = gasUsed.last();
			gasUsed.pop_back();
			vol.mliter = gasPair.second;
			gasUsedString.append(gasPair.first).append(": ").append(get_volume_string(vol, true)).append("\n");
		}
		if (!gasUsed.isEmpty())
			gasUsedString.append("...");
		volume_t o2_tot = {}, he_tot = {};
		selected_dives_gas_parts(&o2_tot, &he_tot);
		gasUsedString.append(QString("These gases could be\nmixed from Air and using:\nHe: %1 and O2: %2\n").arg(get_volume_string(he_tot, true)).arg(get_volume_string(o2_tot, true)));
		ui.gasConsumption->setText(gasUsedString);
	} else {
		/* clear the fields */
		clearInfo();
		clearStats();
		clearEquipment();
		ui.rating->setCurrentStars(0);
		ui.coordinates->clear();
		ui.visibility->setCurrentStars(0);
	}
	editMode = NONE;
}

void MainTab::addCylinder_clicked()
{
	if (editMode == NONE)
		enableEdition();
	cylindersModel->add();
}

void MainTab::addWeight_clicked()
{
	if (editMode == NONE)
		enableEdition();
	weightModel->add();
}

void MainTab::reload()
{
	suitModel.updateModel();
	buddyModel.updateModel();
	locationModel.updateModel();
	diveMasterModel.updateModel();
	tagModel.updateModel();
}

// tricky little macro to edit all the selected dives
// loop over all dives, for each selected dive do WHAT, but do it
// last for the current dive; this is required in case the invocation
// wants to compare things to the original value in current_dive like it should
#define MODIFY_SELECTED_DIVES(WHAT)                            \
	do {                                                 \
		struct dive *mydive = NULL;                  \
		int _i;                                      \
		for_each_dive (_i, mydive) {                 \
			if (!mydive->selected || mydive == cd) \
				continue;                    \
							     \
			WHAT;                                \
		}                                            \
		mydive = cd;                                 \
		WHAT;                                        \
		mark_divelist_changed(true);                 \
	} while (0)

#define EDIT_TEXT(what)                                      \
	if (same_string(mydive->what, cd->what)) {           \
		free(mydive->what);                          \
		mydive->what = strdup(displayed_dive.what);  \
	}

#define EDIT_VALUE(what)                                     \
	if (mydive->what == cd->what) {                      \
		mydive->what = displayed_dive.what;          \
	}

void MainTab::acceptChanges()
{
	int i;
	struct dive *d;
	tabBar()->setTabIcon(0, QIcon()); // Notes
	tabBar()->setTabIcon(1, QIcon()); // Equipment
	hideMessage();
	ui.equipmentTab->setEnabled(true);
	/* now figure out if things have changed */
	if (MainWindow::instance() && MainWindow::instance()->dive_list()->selectedTrips().count() == 1) {
		if (!same_string(displayed_dive.notes, current_dive->divetrip->notes)) {
			current_dive->divetrip->notes = strdup(displayed_dive.notes);
			mark_divelist_changed(true);
		}
		if (!same_string(displayed_dive.location, current_dive->divetrip->location)) {
			current_dive->divetrip->location = strdup(displayed_dive.location);
			mark_divelist_changed(true);
		}
		ui.dateEdit->setEnabled(true);
	} else {
		struct dive *cd = current_dive;
		//Reset coordinates field, in case it contains garbage.
		updateGpsCoordinates(&displayed_dive);
		// now check if something has changed and if yes, edit the selected dives that
		// were identical with the master dive shown (and mark the divelist as changed)
		if (!same_string(displayed_dive.buddy, cd->buddy))
			MODIFY_SELECTED_DIVES(EDIT_TEXT(buddy));
		if (!same_string(displayed_dive.suit, cd->suit))
			MODIFY_SELECTED_DIVES(EDIT_TEXT(suit));
		if (!same_string(displayed_dive.notes, cd->notes))
			MODIFY_SELECTED_DIVES(EDIT_TEXT(notes));
		if (!same_string(displayed_dive.divemaster, cd->divemaster))
			MODIFY_SELECTED_DIVES(EDIT_TEXT(divemaster));
		if (displayed_dive.rating != cd->rating)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(rating));
		if (displayed_dive.visibility != cd->visibility)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(visibility));
		if (displayed_dive.airtemp.mkelvin != cd->airtemp.mkelvin)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(airtemp.mkelvin));
		if (displayed_dive.watertemp.mkelvin != cd->watertemp.mkelvin)
			MODIFY_SELECTED_DIVES(EDIT_VALUE(watertemp.mkelvin));
		if (displayed_dive.when != cd->when) {
			time_t offset = current_dive->when - displayed_dive.when;
			MODIFY_SELECTED_DIVES(mydive->when -= offset;);
		}
		if (!same_string(displayed_dive.location, cd->location)) {
			MODIFY_SELECTED_DIVES(EDIT_TEXT(location));
			// if we have a location text and haven't edited the coordinates, try to fill the coordinates
			// from the existing dives
			if (!same_string(cd->location, "") &&
			    (!ui.coordinates->isModified() ||
			     ui.coordinates->text().trimmed().isEmpty())) {
				struct dive *dive;
				int i = 0;
				for_each_dive (i, dive) {
					QString location(dive->location);
					if (location == ui.location->text() &&
					    (dive->latitude.udeg || dive->longitude.udeg)) {
						MODIFY_SELECTED_DIVES(if (same_string(mydive->location, dive->location)) {
										mydive->latitude = dive->latitude;
										mydive->longitude = dive->longitude;
									});
						MainWindow::instance()->globe()->reload();
						break;
					}
				}
			}
		}
		if (displayed_dive.latitude.udeg != current_dive->latitude.udeg ||
		    displayed_dive.longitude.udeg != current_dive->longitude.udeg) {
			MODIFY_SELECTED_DIVES(gpsHasChanged(mydive, cd, ui.coordinates->text(), 0));
		}
		if (tagsChanged(&displayed_dive, cd))
			saveTags();
		if (editMode == MANUALLY_ADDED_DIVE) {
			DivePlannerPointsModel::instance()->copyCylinders(cd);
		} else if (editMode != ADD && cylindersModel->changed) {
			mark_divelist_changed(true);
			MODIFY_SELECTED_DIVES(
				for (int i = 0; i < MAX_CYLINDERS; i++) {
					if (mydive != cd) {
						if (same_string(mydive->cylinder[i].type.description, cd->cylinder[i].type.description))
							// only copy the cylinder type, none of the other values
							mydive->cylinder[i].type = displayed_dive.cylinder[i].type;
					} else {
						mydive->cylinder[i] = displayed_dive.cylinder[i];
					}
				}
			);
			MainWindow::instance()->graphics()->replot();
		}

		if (weightModel->changed) {
			mark_divelist_changed(true);
			MODIFY_SELECTED_DIVES(
				for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
					if (same_string(mydive->weightsystem[i].description, cd->weightsystem[i].description))
						mydive->weightsystem[i] = displayed_dive.weightsystem[i];
				}
			);
		}
		// each dive that was selected might have had the temperatures in its active divecomputer changed
		// so re-populate the temperatures - easiest way to do this is by calling fixup_dive
		for_each_dive (i, d) {
			if (d->selected)
				fixup_dive(d);
		}
	}
	if (current_dive->divetrip) {
		current_dive->divetrip->when = current_dive->when;
		find_new_trip_start_time(current_dive->divetrip);
	}
	if (editMode == ADD || editMode == MANUALLY_ADDED_DIVE) {
		// clean up the dive data (get duration, depth information from samples)
		// remove the pressures from the samples (as those prevent the user from
		// being able to manually set the start and end pressure)
		struct sample *sample = current_dc->sample;
		for (int i = 0; i < current_dc->samples; i++, sample++)
			sample->cylinderpressure.mbar = 0;
		for (int i = 0; i < MAX_CYLINDERS; i++) {
			cylinder_t *cyl = &current_dive->cylinder[i];
			cyl->start.mbar = cyl->sample_start.mbar;
			cyl->end.mbar = cyl->sample_end.mbar;
			cyl->sample_end.mbar = cyl->sample_start.mbar = 0;
		}
		fixup_dive(current_dive);
		if (dive_table.nr == 1)
			current_dive->number = 1;
		else if (selected_dive == dive_table.nr - 1 && get_dive(dive_table.nr - 2)->number)
			current_dive->number = get_dive(dive_table.nr - 2)->number + 1;
		MainWindow::instance()->showProfile();
		mark_divelist_changed(true);
		DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::NOTHING);
	}
	int scrolledBy = MainWindow::instance()->dive_list()->verticalScrollBar()->sliderPosition();
	resetPallete();
	if (editMode == ADD || editMode == MANUALLY_ADDED_DIVE) {
		// it's tricky to keep the right dive selected;
		// first remember which one is selected in the current sort order
		// and unselect all dives
		int rememberSelected = selected_dive;
		MainWindow::instance()->dive_list()->unselectDives();
		struct dive *d = get_dive(rememberSelected);
		// mark the previously selected dive as remembered (abusing the selected flag)
		// and then clear that flag out on the other side of the sort_table()
		d->selected = true;
		sort_table(&dive_table);
		for_each_dive (rememberSelected, d) {
			if (d->selected) {
				d->selected = false;
				break;
			}
		}
		// refreshDisplay() will select the top dive if no dive was
		// selected - but that may not be the right one, so select the one
		// we remembered instead
		MainWindow::instance()->dive_list()->selectDive(rememberSelected, true);

		editMode = NONE;
		MainWindow::instance()->refreshDisplay();
		MainWindow::instance()->graphics()->replot();
		emit addDiveFinished();
	} else {
		editMode = NONE;
		MainWindow::instance()->dive_list()->rememberSelection();
		sort_table(&dive_table);
		MainWindow::instance()->refreshDisplay();
		MainWindow::instance()->dive_list()->restoreSelection();
	}
	updateDiveInfo();
	DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::NOTHING);
	MainWindow::instance()->dive_list()->verticalScrollBar()->setSliderPosition(scrolledBy);
	MainWindow::instance()->dive_list()->setFocus();
}

void MainTab::resetPallete()
{
	QPalette p;
	ui.buddy->setPalette(p);
	ui.notes->setPalette(p);
	ui.location->setPalette(p);
	ui.coordinates->setPalette(p);
	ui.divemaster->setPalette(p);
	ui.suit->setPalette(p);
	ui.airtemp->setPalette(p);
	ui.watertemp->setPalette(p);
	ui.dateEdit->setPalette(p);
	ui.timeEdit->setPalette(p);
	ui.tagWidget->setPalette(p);
}

#define EDIT_TEXT2(what, text)         \
	textByteArray = text.toUtf8(); \
	free(what);                    \
	what = strdup(textByteArray.data());

#define FREE_IF_DIFFERENT(what)              \
	if (displayed_dive.what != cd->what) \
		free(displayed_dive.what)

void MainTab::rejectChanges()
{
	EditMode lastMode = editMode;

	if (lastMode != NONE && current_dive &&
	    (modified ||
	     memcmp(&current_dive->cylinder[0], &displayed_dive.cylinder[0], sizeof(cylinder_t) * MAX_CYLINDERS) ||
	     memcmp(&current_dive->cylinder[0], &displayed_dive.weightsystem[0], sizeof(weightsystem_t) * MAX_WEIGHTSYSTEMS))) {
		if (QMessageBox::warning(MainWindow::instance(), TITLE_OR_TEXT(tr("Discard the Changes?"),
									       tr("You are about to discard your changes.")),
					 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Discard) != QMessageBox::Discard) {
			return;
		}
	}
	editMode = NONE;
	tabBar()->setTabIcon(0, QIcon()); // Notes
	tabBar()->setTabIcon(1, QIcon()); // Equipment

	if (MainWindow::instance() && MainWindow::instance()->dive_list()->selectedTrips().count() != 1) {
		if (lastMode == ADD) {
			// clean up
			DivePlannerPointsModel::instance()->cancelPlan();
			hideMessage();
			resetPallete();
			return;
		} else if (lastMode == MANUALLY_ADDED_DIVE) {
			// when we tried to edit a manually added dive, we destroyed
			// the dive we edited, so let's just restore it from backup
			DivePlannerPointsModel::instance()->restoreBackupDive();
		}
		if (selected_dive >= 0) {
			copy_dive(current_dive, &displayed_dive);
			cylindersModel->setDive(&displayed_dive);
			weightModel->setDive(&displayed_dive);
		} else {
			cylindersModel->clear();
			weightModel->clear();
			setEnabled(false);
		}
	}
#if 0 // this makes no sense anymore - but let's make sure I think this through
	// now let's avoid memory leaks
	if (MainWindow::instance() && MainWindow::instance()->dive_list()->selectedTrips().count() == 1) {
		if (displayed_dive.location != current_dive->divetrip->location)
			free(displayed_dive.location);
		if (displayed_dive.notes != current_dive->divetrip->notes)
			free(displayed_dive.notes);
	} else {
		struct dive *cd = current_dive;
		FREE_IF_DIFFERENT(tag_list);
		FREE_IF_DIFFERENT(location);
		FREE_IF_DIFFERENT(buddy);
		FREE_IF_DIFFERENT(divemaster);
		FREE_IF_DIFFERENT(notes);
		FREE_IF_DIFFERENT(suit);
	}
#endif
	hideMessage();
	MainWindow::instance()->dive_list()->setEnabled(true);
	ui.dateEdit->setEnabled(true);
	resetPallete();
	MainWindow::instance()->globe()->reload();
	if (lastMode == MANUALLY_ADDED_DIVE) {
		// more clean up
		updateDiveInfo();
		MainWindow::instance()->showProfile();
		// we already reloaded the divelist above, so don't recreate it or we'll lose the selection
		MainWindow::instance()->refreshDisplay(false);
	}
	MainWindow::instance()->dive_list()->setFocus();
	// the user could have edited the location and then canceled the edit
	// let's get the correct location back in view
	MainWindow::instance()->globe()->centerOnCurrentDive();
	DivePlannerPointsModel::instance()->setPlanMode(DivePlannerPointsModel::NOTHING);
	updateDiveInfo();
}
#undef EDIT_TEXT2

void MainTab::markChangedWidget(QWidget *w)
{
	QPalette p;
	qreal h, s, l, a;
	enableEdition();
	qApp->palette().color(QPalette::Text).getHslF(&h, &s, &l, &a);
	p.setBrush(QPalette::Base, (l <= 0.3) ? QColor(Qt::yellow).lighter() : (l <= 0.6) ? QColor(Qt::yellow).light() : /* else */ QColor(Qt::yellow).darker(300));
	w->setPalette(p);
	if (!modified) {
		modified = true;
		enableEdition();
	}
}

void MainTab::on_buddy_textChanged()
{
	if (editMode == IGNORE)
		return;
	QStringList text_list = ui.buddy->toPlainText().split(",", QString::SkipEmptyParts);
	for (int i = 0; i < text_list.size(); i++)
		text_list[i] = text_list[i].trimmed();
	QString text = text_list.join(", ");
	free(displayed_dive.buddy);
	displayed_dive.buddy = strdup(text.toUtf8().data());
	markChangedWidget(ui.buddy);
}

void MainTab::on_divemaster_textChanged()
{
	if (editMode == IGNORE)
		return;
	QStringList text_list = ui.divemaster->toPlainText().split(",", QString::SkipEmptyParts);
	for (int i = 0; i < text_list.size(); i++)
		text_list[i] = text_list[i].trimmed();
	QString text = text_list.join(", ");
	free(displayed_dive.divemaster);
	displayed_dive.divemaster = strdup(text.toUtf8().data());
	markChangedWidget(ui.divemaster);
}

void MainTab::on_airtemp_textChanged(const QString &text)
{
	if (editMode == IGNORE)
		return;
	displayed_dive.airtemp.mkelvin = parseTemperatureToMkelvin(text);
	markChangedWidget(ui.airtemp);
	validate_temp_field(ui.airtemp, text);
}

void MainTab::on_watertemp_textChanged(const QString &text)
{
	if (editMode == IGNORE)
		return;
	displayed_dive.watertemp.mkelvin = parseTemperatureToMkelvin(text);
	markChangedWidget(ui.watertemp);
	validate_temp_field(ui.watertemp, text);
}

void MainTab::validate_temp_field(QLineEdit *tempField, const QString &text)
{
	static bool missing_unit = false;
	static bool missing_precision = false;
	if (!text.contains(QRegExp("^[-+]{0,1}[0-9]+([,.][0-9]+){0,1}(°[CF]){0,1}$")) &&
	    !text.isEmpty() &&
	    !text.contains(QRegExp("^[-+]$"))) {
		if (text.contains(QRegExp("^[-+]{0,1}[0-9]+([,.][0-9]+){0,1}(°)$")) && !missing_unit) {
			if (!missing_unit) {
				missing_unit = true;
				return;
			}
		}
		if (text.contains(QRegExp("^[-+]{0,1}[0-9]+([,.]){0,1}(°[CF]){0,1}$")) && !missing_precision) {
			if (!missing_precision) {
				missing_precision = true;
				return;
			}
		}
		QPalette p;
		p.setBrush(QPalette::Base, QColor(Qt::red).lighter());
		tempField->setPalette(p);
	} else {
		missing_unit = false;
		missing_precision = false;
	}
}

void MainTab::on_dateEdit_dateChanged(const QDate &date)
{
	if (editMode == IGNORE)
		return;
	QDateTime dateTime = QDateTime::fromTime_t(displayed_dive.when);
	dateTime.setTimeSpec(Qt::UTC);
	dateTime.setDate(date);
	displayed_dive.when = dateTime.toTime_t();
	markChangedWidget(ui.dateEdit);
}

void MainTab::on_timeEdit_timeChanged(const QTime &time)
{
	if (editMode == IGNORE)
		return;
	QDateTime dateTime = QDateTime::fromTime_t(displayed_dive.when);
	dateTime.setTimeSpec(Qt::UTC);
	dateTime.setTime(time);
	displayed_dive.when = dateTime.toTime_t();
	markChangedWidget(ui.timeEdit);
}

bool MainTab::tagsChanged(dive *a, dive *b)
{
	char bufA[1024], bufB[1024];
	taglist_get_tagstring(a->tag_list, bufA, sizeof(bufA));
	taglist_get_tagstring(b->tag_list, bufB, sizeof(bufB));
	QString tagStringA(bufA);
	QString tagStringB(bufB);
	QStringList text_list = tagStringA.split(",", QString::SkipEmptyParts);
	for (int i = 0; i < text_list.size(); i++)
		text_list[i] = text_list[i].trimmed();
	QString textA = text_list.join(", ");
	text_list = tagStringB.split(",", QString::SkipEmptyParts);
	for (int i = 0; i < text_list.size(); i++)
		text_list[i] = text_list[i].trimmed();
	QString textB = text_list.join(", ");
	return textA != textB;
}

// changing the tags on multiple dives is semantically strange - what's the right thing to do?
void MainTab::saveTags()
{
	struct dive *cd = current_dive;
	MODIFY_SELECTED_DIVES(
		QString tag;
		taglist_free(mydive->tag_list);
		mydive->tag_list = NULL;
		Q_FOREACH (tag, ui.tagWidget->getBlockStringList())
			taglist_add_tag(&mydive->tag_list, tag.toUtf8().data()););
}

void MainTab::on_tagWidget_textChanged()
{
	if (editMode == IGNORE)
		return;
	QString tag;
	if (displayed_dive.tag_list != current_dive->tag_list)
		taglist_free(displayed_dive.tag_list);
	displayed_dive.tag_list = NULL;
	Q_FOREACH (tag, ui.tagWidget->getBlockStringList())
		taglist_add_tag(&displayed_dive.tag_list, tag.toUtf8().data());
	if (tagsChanged(&displayed_dive, current_dive))
		markChangedWidget(ui.tagWidget);
}

void MainTab::on_location_textChanged(const QString &text)
{
	if (editMode == IGNORE)
		return;
	free(displayed_dive.location);
	displayed_dive.location = strdup(ui.location->text().toUtf8().data());
	markChangedWidget(ui.location);
}

void MainTab::on_suit_textChanged(const QString &text)
{
	if (editMode == IGNORE)
		return;
	free(displayed_dive.suit);
	displayed_dive.suit = strdup(text.toUtf8().data());
	markChangedWidget(ui.suit);
}

void MainTab::on_notes_textChanged()
{
	if (editMode == IGNORE)
		return;
	free(displayed_dive.notes);
	displayed_dive.notes = strdup(ui.notes->toPlainText().toUtf8().data());
	markChangedWidget(ui.notes);
}

void MainTab::on_coordinates_textChanged(const QString &text)
{
	if (editMode == IGNORE)
		return;
	bool gpsChanged = false;
	bool parsed = false;
	QPalette p;
	ui.coordinates->setPalette(p); // reset palette
	gpsChanged = gpsHasChanged(&displayed_dive, current_dive, text, &parsed);
	if (gpsChanged)
		markChangedWidget(ui.coordinates); // marks things yellow
	if (!parsed) {
		p.setBrush(QPalette::Base, QColor(Qt::red).lighter());
		ui.coordinates->setPalette(p); // marks things red
	}
}

void MainTab::on_rating_valueChanged(int value)
{
	if (displayed_dive.rating != value) {
		displayed_dive.rating = value;
		modified = true;
		enableEdition();
	}
}

void MainTab::on_visibility_valueChanged(int value)
{
	if (displayed_dive.visibility != value) {
		displayed_dive.visibility = value;
		modified = true;
		enableEdition();
	}
}

#undef MODIFY_SELECTED_DIVES
#undef EDIT_TEXT
#undef EDIT_VALUE

void MainTab::editCylinderWidget(const QModelIndex &index)
{
	// we need a local copy or bad things happen when enableEdition() is called
	QModelIndex editIndex = index;
	if (cylindersModel->changed && editMode == NONE) {
		enableEdition();
		return;
	}
	if (editIndex.isValid() && editIndex.column() != CylindersModel::REMOVE) {
		if (editMode == NONE)
			enableEdition();
		ui.cylinders->edit(editIndex);
	}
}

void MainTab::editWeightWidget(const QModelIndex &index)
{
	if (editMode == NONE)
		enableEdition();

	if (index.isValid() && index.column() != WeightModel::REMOVE)
		ui.weights->edit(index);
}

void MainTab::updateCoordinatesText(qreal lat, qreal lon)
{
	int ulat = rint(lat * 1000000);
	int ulon = rint(lon * 1000000);
	ui.coordinates->setText(printGPSCoords(ulat, ulon));
}

void MainTab::updateGpsCoordinates(const struct dive *dive)
{
	if (dive) {
		ui.coordinates->setText(printGPSCoords(dive->latitude.udeg, dive->longitude.udeg));
		ui.coordinates->setModified(dive->latitude.udeg || dive->longitude.udeg);
	} else {
		ui.coordinates->clear();
	}
}

QString MainTab::trHemisphere(const char *orig)
{
	return tr(orig);
}

void MainTab::escDetected()
{
	if (editMode != NONE)
		rejectChanges();
}

void MainTab::photoDoubleClicked(const QString filePath)
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}
