
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

PerfTreeColumn::PerfTreeColumn(QString name, QVariant (*getValue)(const PerfTreeItem *item), bool is_duration, bool align_right,
			       bool default_hidden)
	: m_get_value(getValue),
	  m_name(name),
	  m_is_duration(is_duration),
	  m_align_right(align_right),
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
			[](const PerfTreeItem *item) { return QVariant(item->sourceType); }, false, false, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.Active")),
			[](const PerfTreeItem *item) { return QVariant(item->rendered); }, false, false, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.Enabled")),
			[](const PerfTreeItem *item) { return QVariant(item->enabled); }, false, false, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.TickAvg")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->tick_avg));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.TickMax")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->tick_max));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderAvg")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				QVariant v = ns_to_ms(item->m_perf->render_avg);
				auto t = v.metaType();
				return v;
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderMax")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_max));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderTotal")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_sum));
			},
			true, true),
#ifndef __APPLE__
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderGpuAvg")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_gpu_avg));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderGpuMax")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_gpu_max));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.RenderGpuTotal")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->render_gpu_sum));
			},
			true, true),
#endif
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncFps")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(item->m_perf->async_input);
			},
			false, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncBest")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_input_best));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncWorst")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_input_worst));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncRenderedFps")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(item->m_perf->async_rendered);
			},
			false, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncRenderedBest")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_rendered_best));
			},
			true, true, true),
		PerfTreeColumn(
			QString::fromUtf8(obs_module_text("PerfViewer.AsyncRenderedWorst")),
			[](const PerfTreeItem *item) {
				if (!item->m_perf || !item->async)
					return QVariant();
				return QVariant(ns_to_ms(item->m_perf->async_rendered_worst));
			},
			true, true, true),
	};

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

	for (int i = 0; i < model->columnCount(); i++)
		treeView->resizeColumnToContents(i);

	loaded = true;
}

static void EnumFilter(obs_source_t *, obs_source_t *child, void *data)
{
	if (obs_source_get_type(child) != OBS_SOURCE_TYPE_FILTER)
		return;
	auto root = static_cast<PerfTreeItem *>(data);
	auto item = new PerfTreeItem(child, root, root->model());
	root->appendChild(item);
}

static bool EnumSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *data)
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

static bool EnumAllSource(void *data, obs_source_t *source)
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

static bool EnumScene(void *data, obs_source_t *source)
{
	if (obs_source_is_group(source))
		return true;

	return EnumAllSource(data, source);
}

static bool EnumNotPrivateSource(void *data, obs_source_t *source)
{
	if (obs_obj_is_private(source))
		return true;
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return true;

	return EnumAllSource(data, source);
}

static bool EnumAll(void *data, obs_source_t *source)
{
	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER) {
		EnumFilter(nullptr, source, data);
		return true;
	}
	return EnumAllSource(data, source);
}

static bool EnumFilterSource(void *data, obs_source_t *source)
{
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER)
		return true;
	EnumFilter(nullptr, source, data);
	return true;
}

static bool EnumTransition(void *data, obs_source_t *source)
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

	delete rootItem;
}

QVariant PerfTreeModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return {};
	if (role == Qt::CheckStateRole) {
		auto item = static_cast<const PerfTreeItem *>(index.internalPointer());
		auto column = columns.at(index.column());
		auto d = column.Value(item);
		if (d.userType() == QMetaType::Bool)
			return d.toBool() ? Qt::Checked : Qt::Unchecked;

	} else if (role == Qt::DisplayRole) {
		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		auto column = columns.at(index.column());
		auto d = column.Value(item);
		if (d.userType() == QMetaType::Bool)
			return {};
		if (d.userType() == QMetaType::Double) {
			if (d.toDouble() == 0.0)
				return {};
			return QString::asprintf("%.02f", d.toDouble());
		}
		return d;

	} else if (role == Qt::DecorationRole) {
		if (index.column() != 0)
			return {};

		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		return item->icon;
	} else if (role == Qt::ForegroundRole) {
		if (!columns.at(index.column()).m_is_duration)
			return {};

		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		auto column = columns.at(index.column());
		auto d = column.Value(item);
		return item->getTextColour(d.toDouble());
	} else if (role == Qt::TextAlignmentRole) {
		if (columns.at(index.column()).m_align_right)
			return Qt::AlignRight;
	} else if (role == Qt::UserRole) {
		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		auto column = columns.at(index.column());
		auto d = column.Value(item);
		return d;
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
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
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
	async = (obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER &&
		 (obs_source_get_output_flags(source) & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO);
	icon = getIcon(source);
	m_perf = new profiler_result_t;
	memset(m_perf, 0, sizeof(profiler_result_t));
}

PerfTreeItem::~PerfTreeItem()
{
	obs_weak_source_release(m_source);
	delete m_perf;
	qDeleteAll(m_childItems);
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
		for (auto item : m_childItems)
			item->update();
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

QVariant PerfTreeItem::getTextColour(double frameTime) const
{
	double target = m_model->targetFrameTime();
	if (frameTime >= target)
		return QBrush(Qt::red);
	if (frameTime >= target * 0.5)
		return QBrush(Qt::yellow);
	if (frameTime >= target * 0.25)
		return QBrush(Qt::darkYellow);

	return {};
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
