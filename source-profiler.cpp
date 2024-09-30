
#include "version.h"

#include "source-profiler.hpp"
#include <obs-frontend-api.h>
#include <QAction>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QHeaderView>
#include <QComboBox>
#include <QMenu>
#include <util/config-file.h>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");

OBS_MODULE_USE_DEFAULT_LOCALE("source-profiler", "en-US")

static OBSPerfViewer *perf_viewer = nullptr;

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Source Profiler] loaded version %s", PROJECT_VERSION);

	QAction *a = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("PerfViewer"));
	QAction::connect(a, &QAction::triggered, []() {
		if (perf_viewer) {
			perf_viewer->activateWindow();
			perf_viewer->raise();
		} else {
			perf_viewer = new OBSPerfViewer();
		}
	});

	return true;
}

OBSPerfViewer::OBSPerfViewer(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("PerfViewer")));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowFlags(windowFlags() & Qt::WindowMaximizeButtonHint & ~Qt::WindowContextHelpButtonHint);
	setSizeGripEnabled(true);
	setGeometry(0, 0, 805, 300);

	model = new PerfTreeModel(this);

	proxy = new PerfViewerProxyModel(this);
	proxy->setSourceModel(model);

	treeView = new QTreeView();
	treeView->setModel(proxy);
	treeView->setSortingEnabled(true);
	treeView->sortByColumn(-1, Qt::AscendingOrder);
	treeView->setAlternatingRowColors(true);
	treeView->setAnimated(true);
	auto tvh = treeView->header();
	tvh->setSortIndicatorShown(true);
	tvh->setSectionsClickable(true);
	tvh->setStretchLastSection(false);

	for (int i : model->getDefaultHiddenColumns())
		tvh->setSectionHidden(i, true);

	tvh->setSortIndicatorClearable(true);
	tvh->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(tvh, &QHeaderView::customContextMenuRequested, this, [&](const QPoint &pos) {
		QMenu menu;
		auto tvh = treeView->header();
		for (int i = 0; i < tvh->count(); i++) {
			auto title = model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
			auto a = menu.addAction(title);
			a->setEnabled(i != 0);
			a->setCheckable(true);
			a->setChecked(!tvh->isSectionHidden(i));
			connect(a, &QAction::triggered, [this, i] {
				auto tvh = treeView->header();
				tvh->setSectionHidden(i, !tvh->isSectionHidden(i));
				if (!tvh->isSectionHidden(i))
					treeView->resizeColumnToContents(i);
			});
		}
		menu.exec(QCursor::pos());
	});

	auto l = new QVBoxLayout();
	l->setContentsMargins(0, 0, 0, 4);

	auto searchBarLayout = new QHBoxLayout();
	auto groupByBox = new QComboBox();
	groupByBox->addItem(QString::fromUtf8(obs_module_text("PerfViewer.Scene")));
	groupByBox->addItem(QString::fromUtf8(obs_module_text("PerfViewer.Source")));
	groupByBox->addItem(QString::fromUtf8(obs_module_text("PerfViewer.Filter")));
	groupByBox->addItem(QString::fromUtf8(obs_module_text("PerfViewer.Transition")));
	groupByBox->addItem(QString::fromUtf8(obs_module_text("PerfViewer.All")));
	searchBarLayout->addWidget(groupByBox);
	searchBarLayout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Expanding));

	auto searchBox = new QLineEdit();
	searchBox->setMinimumSize(256, 0);
	searchBox->setPlaceholderText(QString::fromUtf8(obs_module_text("PerfViewer.Search")));
	searchBarLayout->addWidget(searchBox);

	l->addLayout(searchBarLayout);

	l->addWidget(treeView);

	auto buttonLayout = new QHBoxLayout();
	buttonLayout->setContentsMargins(10, 0, 10, 0);

	buttonLayout->addSpacerItem(new QSpacerItem(40, 20, QSizePolicy::Expanding));
	auto refreshLabel = new QLabel(QString::fromUtf8(obs_module_text("PerfViewer.RefreshInterval")));
	buttonLayout->addWidget(refreshLabel);

	auto refreshInterval = new QSpinBox();
	refreshInterval->setSuffix(" ms");
	refreshInterval->setMinimum(500);
	refreshInterval->setMaximum(10000);
	refreshInterval->setSingleStep(100);
	refreshInterval->setValue(1000);
	refreshLabel->setBuddy(refreshInterval);
	buttonLayout->addWidget(refreshInterval);

	auto resetButton = new QPushButton(QString::fromUtf8(obs_frontend_get_locale_string("Reset")));
	buttonLayout->addWidget(resetButton);

	auto closeButton = new QPushButton(QString::fromUtf8(obs_frontend_get_locale_string("Close")));
	buttonLayout->addWidget(closeButton);
	l->addLayout(buttonLayout);

	setLayout(l);

	connect(closeButton, &QPushButton::clicked, this, &OBSPerfViewer::close);
	connect(resetButton, &QAbstractButton::clicked, model, &PerfTreeModel::refreshSources);
	connect(model, &PerfTreeModel::modelReset, this, &OBSPerfViewer::sourceListUpdated);
	connect(groupByBox, &QComboBox::currentIndexChanged, this, [&](int index) {
		if (index < 0 || model->getShowMode() == index)
			return;
		model->setShowMode((PerfTreeModel::ShowMode)index);
		treeView->collapseAll();
	});
	connect(searchBox, &QLineEdit::textChanged, this, [&](const QString &text) {
		proxy->setFilterText(text);
		treeView->expandAll();
	});
	connect(refreshInterval, &QSpinBox::valueChanged, model, &PerfTreeModel::setRefreshInterval);

	source_profiler_enable(true);
#ifndef __APPLE__
	source_profiler_gpu_enable(true);
#endif

	model->refreshSources();

	auto obs_config = obs_frontend_get_user_config();

	const char *geom = config_get_string(obs_config, "PerfViewer", "geometry");
	if (geom != nullptr) {
		QByteArray ba = QByteArray::fromBase64(QByteArray(geom));
		restoreGeometry(ba);
	}

	groupByBox->setCurrentIndex(config_get_int(obs_config, "PerfViewer", "showmode"));

	const char *columns = config_get_string(obs_config, "PerfViewer", "columns");
	if (columns != nullptr) {
		QByteArray ba = QByteArray::fromBase64(QByteArray(columns));
		treeView->header()->restoreState(ba);
	}

	show();
}

OBSPerfViewer::~OBSPerfViewer()
{
	perf_viewer = nullptr;
	const auto obs_config = obs_frontend_get_user_config();
	if (obs_config) {
		config_set_string(obs_config, "PerfViewer", "columns", treeView->header()->saveState().toBase64().constData());
		config_set_string(obs_config, "PerfViewer", "geometry", saveGeometry().toBase64().constData());
		config_set_int(obs_config, "PerfViewer", "showmode", model->getShowMode());
	}
#ifndef __APPLE__
	source_profiler_gpu_enable(false);
#endif
	source_profiler_enable(false);
	delete model;
}

PerfTreeColumn::PerfTreeColumn(QString name, QVariant (*getValue)(const PerfTreeItem *item), enum PerfTreeColumnType column_type,
			       bool default_hidden)
	: m_get_value(getValue),
	  m_name(name),
	  m_column_type(column_type),
	  m_default_hidden(default_hidden)
{
}

static double ns_to_ms(uint64_t ns)
{
	return (double)ns / 1000000.0;
}

PerfTreeModel::PerfTreeModel(QObject *parent) : QAbstractItemModel(parent)
{
	columns = {
		PerfTreeColumn(QString::fromUtf8(obs_module_text("PerfViewer.Name")),
			       [](const PerfTreeItem *item) { return QVariant(item->name); }),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.Type")),
			[](const PerfTreeItem *item) { return QVariant(item->sourceType); }, DEFAULT, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.Active")),
			[](const PerfTreeItem *item) { return QVariant(item->rendered); }, BOOL, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.Enabled")),
			[](const PerfTreeItem *item) { return QVariant(item->enabled); }, BOOL, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.TickAvg")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->tick_avg));
			},
			DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.TickMax")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->tick_max));
			},
			DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderAvg")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				QVariant v = ns_to_ms(item->m_perf->render_avg);
				auto t = v.metaType();
				return v;
			},
			DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderMax")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_max));
			},
			DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderTotal")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_sum));
			},
			DURATION),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.CpuPercentage")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant((double)(item->m_perf->render_sum + item->m_perf->tick_avg) /
						(double)obs_get_frame_interval_ns() * 100.0);
			},
			PERCENTAGE),
#ifndef __APPLE__
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderGpuAvg")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_gpu_avg));
			},
			PerfTreeColumnType::DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderGpuMax")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_gpu_max));
			},
			PerfTreeColumnType::DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderGpuTotal")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_gpu_sum));
			},
			PerfTreeColumnType::DURATION),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.GpuPercentage")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant((double)item->m_perf->render_gpu_sum / (double)obs_get_frame_interval_ns() * 100.0);
			},
			PerfTreeColumnType::PERCENTAGE, true),
#endif
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncFps")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(item->m_perf->async_input);
			},
			PerfTreeColumnType::FPS, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncBest")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_input_best));
			},
			PerfTreeColumnType::DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncWorst")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_input_worst));
			},
			PerfTreeColumnType::DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncRenderedFps")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(item->m_perf->async_rendered);
			},
			PerfTreeColumnType::FPS, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncRenderedBest")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_rendered_best));
			},
			PerfTreeColumnType::DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncRenderedWorst")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_rendered_worst));
			},
			PerfTreeColumnType::DURATION, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.Total")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(
					ns_to_ms(item->m_perf->tick_avg + item->m_perf->render_sum + item->m_perf->render_gpu_sum));
			},
			PerfTreeColumnType::DURATION),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.TotalPercentage")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(
					(double)(item->m_perf->tick_avg + item->m_perf->render_sum + item->m_perf->render_gpu_sum) /
					(double)obs_get_frame_interval_ns() * 100.0);
			},
			PerfTreeColumnType::PERCENTAGE),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.SubItems")),
			[](const PerfTreeItem *item) { return QVariant(item->child_count); }, PerfTreeColumnType::COUNT),
	};

	auto sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create", source_add, this);
	signal_handler_connect(sh, "source_destroy", source_remove, this);
	signal_handler_connect(sh, "source_remove", source_remove, this);

	obs_frontend_add_event_callback(frontend_event, this);

	updater.reset(new QuickThread([this] {
		while (true) {
			QThread::msleep(refreshInterval);
			updateData();
		}
	}));

	updater->start();
}

QList<int> PerfTreeModel::getDefaultHiddenColumns()
{
	QList<int> hiddenColumns;
	for (int i = 0; i < columns.count(); i++) {
		auto column = columns.at(i);
		if (column.DefaultHidden())
			hiddenColumns.append(i);
	}
	return hiddenColumns;
}

void OBSPerfViewer::sourceListUpdated()
{
	if (loaded)
		return;

	for (int i = 0; i < model->columnCount(); i++) {
		if (!treeView->isColumnHidden(i))
			treeView->resizeColumnToContents(i);
	}

	loaded = true;
}

void PerfTreeModel::EnumFilter(obs_source_t *, obs_source_t *child, void *data)
{
	if (obs_source_get_type(child) != OBS_SOURCE_TYPE_FILTER)
		return;
	auto root = static_cast<PerfTreeItem *>(data);
	auto item = new PerfTreeItem(child, root, root->model());
	root->appendChild(item);
}

bool PerfTreeModel::EnumSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto parent = static_cast<PerfTreeItem *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);

	auto treeItem = new PerfTreeItem(item, parent, parent->model());
	parent->prependChild(treeItem);

	if (obs_source_is_scene(source)) {
		obs_scene_t *scene = obs_scene_from_source(source);
		obs_scene_enum_items(scene, EnumSceneItem, treeItem);
	} else if (obs_sceneitem_is_group(item)) {
		obs_scene_t *scene = obs_sceneitem_group_get_scene(item);
		obs_scene_enum_items(scene, EnumSceneItem, treeItem);
	}
	if (obs_source_filter_count(source) > 0) {
		obs_source_enum_filters(source, EnumFilter, treeItem);
	}
	return true;
}

bool PerfTreeModel::EnumAllSource(void *data, obs_source_t *source)
{
	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER)
		return true;

	auto root = static_cast<PerfTreeItem *>(data);
	auto item = new PerfTreeItem(source, root, root->model());
	root->appendChild(item);

	if (obs_scene_t *scene = obs_scene_from_source(source)) {
		obs_scene_enum_items(scene, EnumSceneItem, item);
	}

	if (obs_source_filter_count(source) > 0) {
		obs_source_enum_filters(source, EnumFilter, item);
	}
	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
		auto transition_source = obs_transition_get_active_source(source);
		if (transition_source) {
			EnumAllSource(item, transition_source);
			obs_source_release(transition_source);
		}
	}

	return true;
}

bool PerfTreeModel::EnumScene(void *data, obs_source_t *source)
{
	if (obs_source_is_group(source))
		return true;

	return EnumAllSource(data, source);
}

bool PerfTreeModel::EnumNotPrivateSource(void *data, obs_source_t *source)
{
	if (obs_obj_is_private(source))
		return true;
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return true;

	return EnumAllSource(data, source);
}

bool PerfTreeModel::EnumAll(void *data, obs_source_t *source)
{
	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER) {
		EnumFilter(nullptr, source, data);
		return true;
	}
	return EnumAllSource(data, source);
}

bool PerfTreeModel::EnumFilterSource(void *data, obs_source_t *source)
{
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER)
		return true;
	EnumFilter(nullptr, source, data);
	return true;
}

bool PerfTreeModel::EnumTransition(void *data, obs_source_t *source)
{
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_TRANSITION)
		return true;
	return EnumAllSource(data, source);
}

void PerfTreeModel::refreshSources()
{
	if (refreshing)
		return;

	refreshing = true;
	beginResetModel();
	delete rootItem;
	rootItem = new PerfTreeItem((obs_source_t *)nullptr, nullptr, this);

	//SCENE, SOURCE, FILTER, TRANSITION, ALL
	if (showMode == ShowMode::ALL) {
		obs_enum_all_sources(EnumAll, rootItem);
	} else if (showMode == ShowMode::SOURCE) {
		obs_enum_all_sources(EnumNotPrivateSource, rootItem);
	} else if (showMode == ShowMode::SCENE) {
		obs_enum_scenes(EnumScene, rootItem);
	} else if (showMode == ShowMode::FILTER) {
		obs_enum_all_sources(EnumFilterSource, rootItem);
	} else if (showMode == ShowMode::TRANSITION) {
		obs_enum_all_sources(EnumTransition, rootItem);
	}
	endResetModel();
	refreshing = false;
}

void PerfTreeModel::updateData()
{
	if (refreshing)
		return;
	// Set target frame time in ms
	frameTime = ns_to_ms(obs_get_frame_interval_ns());

	if (rootItem)
		rootItem->update();
}

void PerfViewerProxyModel::setFilterText(const QString &filter)
{
	QRegularExpression regex(filter, QRegularExpression::CaseInsensitiveOption);
	setFilterRegularExpression(regex);
}

bool PerfViewerProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	QModelIndex itemIndex = sourceModel()->index(sourceRow, 0, sourceParent);

	auto name = sourceModel()->data(itemIndex, Qt::DisplayRole).toString();
	return name.contains(filterRegularExpression());
}

PerfTreeModel::~PerfTreeModel()
{
	if (updater)
		updater->terminate();

	obs_frontend_remove_event_callback(frontend_event, this);

	auto sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create", source_add, this);
	signal_handler_disconnect(sh, "source_destroy", source_remove, this);
	signal_handler_disconnect(sh, "source_remove", source_remove, this);

	delete rootItem;
}

QColor ColorFormPercentage(double percentage)
{
	if (obs_frontend_is_theme_dark()) {
		// https://coolors.co/palette/11151c-212d40-364156-7d4e57
		if (percentage >= 100.0)
			return QColor(125, 78, 87);
		if (percentage >= 50.0)
			return QColor(54, 65, 86);
		if (percentage >= 25.0)
			return QColor(33, 45, 64);
		return QColor(17, 21, 28);
	}
	// https://coolors.co/palette/e2e2df-d2d2cf-e2cfc4-f7d9c4
	if (percentage >= 100.0)
		return QColor(247, 217, 196);
	if (percentage >= 50.0)
		return QColor(226, 207, 196);
	if (percentage >= 25.0)
		return QColor(210, 210, 207);
	return QColor(226, 226, 223);
}

QVariant PerfTreeModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return {};
	if (role == Qt::CheckStateRole) {
		auto column = columns.at(index.column());
		if (column.m_column_type != BOOL)
			return {};
		auto item = static_cast<const PerfTreeItem *>(index.internalPointer());
		auto d = column.Value(item);
		if (d.userType() == QMetaType::Bool)
			return d.toBool() ? Qt::Checked : Qt::Unchecked;

	} else if (role == Qt::DisplayRole) {
		auto column = columns.at(index.column());
		if (column.m_column_type == BOOL)
			return {};
		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		auto d = column.Value(item);
		if (d.userType() == QMetaType::Bool)
			return {};
		if (d.userType() == QMetaType::Double) {
			if (d.toDouble() < 0.005)
				return {};
			return QString::asprintf("%.02f", d.toDouble());
		}
		return d;

	} else if (role == Qt::DecorationRole) {
		if (index.column() != 0)
			return {};

		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		return item->icon;
	} else if (role == Qt::BackgroundRole) {
		auto column_type = columns.at(index.column()).m_column_type;
		if (column_type == PERCENTAGE) {
			auto item = static_cast<PerfTreeItem *>(index.internalPointer());
			auto column = columns.at(index.column());
			return ColorFormPercentage(column.Value(item).toDouble());

		} else if (column_type == DURATION) {
			if (frameTime <= 0.0)
				return {};
			auto item = static_cast<PerfTreeItem *>(index.internalPointer());
			auto column = columns.at(index.column());
			return ColorFormPercentage(column.Value(item).toDouble() / frameTime * 100.0);
		}
		return {};
	} else if (role == Qt::TextAlignmentRole) {
		if (columns.at(index.column()).m_column_type != DEFAULT)
			return Qt::AlignRight;
	} else if (role == Qt::UserRole) {
		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		auto column = columns.at(index.column());
		auto d = column.Value(item);
		return d;
	} else if (role == Qt::InitialSortOrderRole) {
		auto column_type = columns.at(index.column()).m_column_type;
		if (column_type == PERCENTAGE || column_type == DURATION)
			return Qt::DescendingOrder;
	}

	return {};
}

Qt::ItemFlags PerfTreeModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return Qt::NoItemFlags;

	return QAbstractItemModel::flags(index);
}

QVariant PerfTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < columns.size()) {
		auto column = columns.at(section);
		return column.Name();
	}

	return QAbstractItemModel::headerData(section, orientation, role);
}

QModelIndex PerfTreeModel::index(int row, int column, const QModelIndex &parent) const
{
	if (!hasIndex(row, column, parent))
		return {};

	PerfTreeItem *parentItem;

	if (!parent.isValid())
		parentItem = rootItem;
	else
		parentItem = static_cast<PerfTreeItem *>(parent.internalPointer());

	if (auto childItem = parentItem->child(row))
		return createIndex(row, column, childItem);

	return {};
}

QModelIndex PerfTreeModel::parent(const QModelIndex &index) const
{
	if (!index.isValid())
		return {};

	auto childItem = static_cast<PerfTreeItem *>(index.internalPointer());
	auto parentItem = childItem->parentItem();

	if (parentItem == rootItem)
		return {};

	return createIndex(parentItem->row(), 0, parentItem);
}

int PerfTreeModel::rowCount(const QModelIndex &parent) const
{
	PerfTreeItem *parentItem;
	if (parent.column() > 0)
		return 0;

	if (!parent.isValid())
		parentItem = rootItem;
	else
		parentItem = static_cast<PerfTreeItem *>(parent.internalPointer());

	if (!parentItem)
		return 0;

	return parentItem->childCount();
}

int PerfTreeModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return static_cast<PerfTreeItem *>(parent.internalPointer())->columnCount();
	return (int)columns.count();
}

void PerfTreeModel::add_filter(obs_source_t *source, obs_source_t *filter, const QModelIndex &parent)
{
	if (refreshing)
		return;
	auto count = rowCount(parent);
	for (int i = 0; i < count; i++) {
		auto index2 = index(i, 0, parent);
		auto item = static_cast<PerfTreeItem *>(index2.internalPointer());
		if (item->m_source && obs_weak_source_references_source(item->m_source, source)) {
			auto pos = rowCount(index2);
			beginInsertRows(index2, pos, pos);
			item->appendChild(new PerfTreeItem(filter, item, this));
			endInsertRows();
		} else {
			add_filter(source, filter, index2);
		}
	}
}

void PerfTreeModel::remove_source(obs_source_t *source, const QModelIndex &parent)
{
	if (refreshing)
		return;
	auto count = rowCount(parent);
	for (int i = count - 1; i >= 0; i--) {
		auto index2 = index(i, 0, parent);
		auto item = static_cast<PerfTreeItem *>(index2.internalPointer());
		if (item->m_source && obs_weak_source_references_source(item->m_source, source)) {
			auto sh = obs_source_get_signal_handler(source);
			signal_handler_disconnect(sh, "filter_add", item->filter_add, item);
			signal_handler_disconnect(sh, "filter_remove", item->filter_remove, item);
			signal_handler_disconnect(sh, "item_add", item->sceneitem_add, item);
			signal_handler_disconnect(sh, "item_remove", item->sceneitem_remove, item);
			remove_siblings(index2);
			beginRemoveRows(parent, i, i);
			item->m_parentItem->m_childItems.removeOne(item);
			endRemoveRows();
			delete item;
		} else {
			remove_source(source, index2);
		}
	}
}

void PerfTreeModel::remove_siblings(const QModelIndex &parent)
{
	auto count = rowCount(parent);
	for (int i = count - 1; i >= 0; i--) {
		auto index2 = index(i, 0, parent);
		auto item = static_cast<PerfTreeItem *>(index2.internalPointer());
		auto source = obs_weak_source_get_source(item->m_source);
		if (source) {
			auto sh = obs_source_get_signal_handler(source);
			signal_handler_disconnect(sh, "filter_add", item->filter_add, item);
			signal_handler_disconnect(sh, "filter_remove", item->filter_remove, item);
			signal_handler_disconnect(sh, "item_add", item->sceneitem_add, item);
			signal_handler_disconnect(sh, "item_remove", item->sceneitem_remove, item);
			obs_source_release(source);
		}
		remove_siblings(index2);
		beginRemoveRows(parent, i, i);
		item->m_parentItem->m_childItems.removeOne(item);
		endRemoveRows();
		delete item;
	}
}

void PerfTreeModel::add_sceneitem(obs_source_t *scene, obs_sceneitem_t *sceneitem, const QModelIndex &parent)
{
	if (refreshing)
		return;
	auto count = rowCount(parent);
	for (int i = 0; i < count; i++) {
		auto index2 = index(i, 0, parent);
		auto item = static_cast<PerfTreeItem *>(index2.internalPointer());
		if (item->m_source && obs_weak_source_references_source(item->m_source, scene)) {
			auto pos = rowCount(index2);
			beginInsertRows(index2, pos, pos);
			item->appendChild(new PerfTreeItem(sceneitem, item, this));
			endInsertRows();
		} else {
			add_sceneitem(scene, sceneitem, index2);
		}
	}
}
void PerfTreeModel::remove_sceneitem(obs_source_t *scene, obs_sceneitem_t *sceneitem, const QModelIndex &parent)
{
	if (refreshing)
		return;
	auto count = rowCount(parent);
	for (int i = count - 1; i >= 0; i--) {
		auto index2 = index(i, 0, parent);
		auto item = static_cast<PerfTreeItem *>(index2.internalPointer());
		if (item->m_sceneitem && item->m_sceneitem == sceneitem) {
			beginRemoveRows(parent, i, i);
			item->m_parentItem->m_childItems.removeOne(item);
			endRemoveRows();
			delete item;
		} else {
			remove_sceneitem(scene, sceneitem, index2);
		}
	}
}

void PerfTreeModel::source_add(void *data, calldata_t *cd)
{
	obs_source_t *source = (obs_source_t *)calldata_ptr(cd, "source");
	auto model = (PerfTreeModel *)data;
	if (model->showMode == ShowMode::SCENE && !obs_source_is_scene(source))
		return;
	if (model->showMode == ShowMode::SOURCE && obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return;
	if (model->showMode == ShowMode::FILTER && obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER)
		return;
	if (model->showMode == ShowMode::TRANSITION && obs_source_get_type(source) != OBS_SOURCE_TYPE_TRANSITION)
		return;

	QModelIndex parent;
	auto pos = model->rowCount(parent);
	model->beginInsertRows(parent, pos, pos);
	model->rootItem->appendChild(new PerfTreeItem(source, model->rootItem, model));
	model->endInsertRows();
}

void PerfTreeModel::source_remove(void *data, calldata_t *cd)
{
	obs_source_t *source = (obs_source_t *)calldata_ptr(cd, "source");
	auto model = (PerfTreeModel *)data;
	model->remove_source(source);
}

void PerfTreeModel::frontend_event(enum obs_frontend_event event, void *data)
{
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP || event == OBS_FRONTEND_EVENT_EXIT ||
	    event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
		auto model = (PerfTreeModel *)data;
		model->refreshing = true;
		model->remove_siblings();
		model->refreshing = false;
	}
}

PerfTreeItem::PerfTreeItem(obs_sceneitem_t *sceneitem, PerfTreeItem *parentItem, PerfTreeModel *model)
	: PerfTreeItem(obs_sceneitem_get_source(sceneitem), parentItem, model)

{
	m_sceneitem = sceneitem;
}

PerfTreeItem::PerfTreeItem(obs_source_t *source, PerfTreeItem *parent, PerfTreeModel *model)
	: m_parentItem(parent),
	  m_model(model),
	  m_source(obs_source_get_weak_source(source))
{
	name = QString::fromUtf8(source ? obs_source_get_name(source) : "");
	sourceType = QString::fromUtf8(source ? obs_source_get_display_name(obs_source_get_unversioned_id(source)) : "");
	is_filter = obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER;
	if (!is_filter && source) {
		auto sh = obs_source_get_signal_handler(source);
		signal_handler_connect(sh, "filter_add", filter_add, this);
		signal_handler_connect(sh, "filter_remove", filter_remove, this);
	}
	if (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE) {
		auto sh = obs_source_get_signal_handler(source);
		signal_handler_connect(sh, "item_add", sceneitem_add, this);
		signal_handler_connect(sh, "item_remove", sceneitem_remove, this);
	}
	async = (!is_filter && (obs_source_get_output_flags(source) & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO);
	icon = getIcon(source);
	m_perf = new profiler_result_t;
	memset(m_perf, 0, sizeof(profiler_result_t));
	while (parent) {
		parent->child_count++;
		parent = parent->m_parentItem;
	}
}

PerfTreeItem::~PerfTreeItem()
{
	auto parent = m_parentItem;
	while (parent) {
		parent->child_count--;
		parent = parent->m_parentItem;
	}
	if (m_source) {
		auto source = obs_weak_source_get_source(m_source);
		if (source) {
			auto sh = obs_source_get_signal_handler(source);
			signal_handler_disconnect(sh, "filter_add", filter_add, this);
			signal_handler_disconnect(sh, "filter_remove", filter_remove, this);
			signal_handler_disconnect(sh, "item_add", sceneitem_add, this);
			signal_handler_disconnect(sh, "item_remove", sceneitem_remove, this);
			obs_source_release(source);
		}
		obs_weak_source_release(m_source);
	}
	qDeleteAll(m_childItems);
	delete m_perf;
}

void PerfTreeItem::appendChild(PerfTreeItem *item)
{
	m_childItems.append(item);
}

void PerfTreeItem::prependChild(PerfTreeItem *item)
{
	m_childItems.prepend(item);
}

PerfTreeItem *PerfTreeItem::child(int row) const
{
	if (row < 0 || row >= m_childItems.size())
		return nullptr;
	return m_childItems.at(row);
}

int PerfTreeItem::childCount() const
{
	return m_childItems.count();
}

int PerfTreeItem::columnCount() const
{
	return m_model->columnCount();
}

int PerfTreeItem::row() const
{
	if (m_parentItem)
		return m_parentItem->m_childItems.indexOf(const_cast<PerfTreeItem *>(this));

	return 0;
}

PerfTreeItem *PerfTreeItem::parentItem()
{
	return m_parentItem;
}

void PerfTreeItem::update()
{
	obs_source_t *source = obs_weak_source_get_source(m_source);
	if (source) {
		if (obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER)
			rendered = m_parentItem->isRendered();
		else
			rendered = obs_source_showing(source);

		enabled = m_sceneitem ? obs_sceneitem_visible(m_sceneitem) : obs_source_enabled(source);

		source_profiler_fill_result(source, m_perf);

		if (m_model)
			m_model->itemChanged(this);
		obs_source_release(source);
	} else if (m_source) {
		enabled = false;
		rendered = false;
		memset(m_perf, 0, sizeof(profiler_result_t));
	}

	if (!m_childItems.empty()) {
		for (auto item : m_childItems) {
			item->update();
			m_perf->tick_avg += item->m_perf->tick_avg;
			m_perf->tick_max += item->m_perf->tick_max;
			if (item->is_filter) {
				m_perf->render_avg += item->m_perf->render_avg;
				m_perf->render_max += item->m_perf->render_max;
				m_perf->render_gpu_avg += item->m_perf->render_gpu_avg;
				m_perf->render_gpu_max += item->m_perf->render_gpu_max;
				m_perf->render_sum += item->m_perf->render_sum;
				m_perf->render_gpu_sum += item->m_perf->render_gpu_sum;
				// async_input
				//async_rendered
				m_perf->async_input_best += item->m_perf->async_input_best;
				m_perf->async_input_worst += item->m_perf->async_input_worst;
				m_perf->async_rendered_best += item->m_perf->async_rendered_best;
				m_perf->async_rendered_worst += item->m_perf->async_rendered_worst;
			}
		}
	}
}

QIcon PerfTreeItem::getIcon(obs_source_t *source) const
{
	// ToDo icons for root?
	if (!source)
		return {};
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	const char *id = obs_source_get_id(source);

	// Todo filter icon from source toolbar
	if (strcmp(id, "scene") == 0)
		return main_window->property("sceneIcon").value<QIcon>();
	else if (strcmp(id, "group") == 0)
		return main_window->property("groupIcon").value<QIcon>();
	else if (obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER)
		return main_window->property("filterIcon").value<QIcon>();

	switch (obs_source_get_icon_type(id)) {
	case OBS_ICON_TYPE_IMAGE:
		return main_window->property("imageIcon").value<QIcon>();
	case OBS_ICON_TYPE_COLOR:
		return main_window->property("colorIcon").value<QIcon>();
	case OBS_ICON_TYPE_SLIDESHOW:
		return main_window->property("slideshowIcon").value<QIcon>();
	case OBS_ICON_TYPE_AUDIO_INPUT:
		return main_window->property("audioInputIcon").value<QIcon>();
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
		return main_window->property("audioOutputIcon").value<QIcon>();
	case OBS_ICON_TYPE_DESKTOP_CAPTURE:
		return main_window->property("desktopCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_WINDOW_CAPTURE:
		return main_window->property("windowCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_GAME_CAPTURE:
		return main_window->property("gameCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_CAMERA:
		return main_window->property("cameraIcon").value<QIcon>();
	case OBS_ICON_TYPE_TEXT:
		return main_window->property("textIcon").value<QIcon>();
	case OBS_ICON_TYPE_MEDIA:
		return main_window->property("mediaIcon").value<QIcon>();
	case OBS_ICON_TYPE_BROWSER:
		return main_window->property("browserIcon").value<QIcon>();
	case OBS_ICON_TYPE_CUSTOM:
		//TODO: Add ability for sources to define custom icons
		return main_window->property("defaultIcon").value<QIcon>();
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT:
		return main_window->property("audioProcessOutputIcon").value<QIcon>();
	default:
		return main_window->property("defaultIcon").value<QIcon>();
	}
}

void PerfTreeItem::filter_add(void *data, calldata_t *cd)
{
	obs_source_t *filter = (obs_source_t *)calldata_ptr(cd, "filter");
	obs_source_t *source = (obs_source_t *)calldata_ptr(cd, "source");
	auto root = static_cast<PerfTreeItem *>(data);
	root->m_model->add_filter(source, filter);
}

void PerfTreeItem::filter_remove(void *data, calldata_t *cd)
{
	obs_source_t *filter = (obs_source_t *)calldata_ptr(cd, "filter");
	auto root = static_cast<PerfTreeItem *>(data);
	root->m_model->remove_source(filter);
}

void PerfTreeItem::sceneitem_add(void *data, calldata_t *cd)
{
	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(cd, "scene");
	obs_sceneitem_t *item = (obs_sceneitem_t *)calldata_ptr(cd, "item");
	auto root = static_cast<PerfTreeItem *>(data);

	root->m_model->add_sceneitem(obs_scene_get_source(scene), item);
}

void PerfTreeItem::sceneitem_remove(void *data, calldata_t *cd)
{
	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(cd, "scene");
	obs_sceneitem_t *item = (obs_sceneitem_t *)calldata_ptr(cd, "item");
	auto root = static_cast<PerfTreeItem *>(data);
	root->m_model->remove_sceneitem(obs_scene_get_source(scene), item);
}

void PerfTreeModel::itemChanged(PerfTreeItem *item)
{
	QModelIndex left = createIndex(item->row(), 0, item);
	QModelIndex right = createIndex(item->row(), item->columnCount() - 1, item);
	emit dataChanged(left, right);
}

void PerfTreeModel::setRefreshInterval(int interval)
{
	refreshInterval = interval;
}
