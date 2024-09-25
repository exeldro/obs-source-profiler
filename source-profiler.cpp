
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
	tvh->setSortIndicatorClearable(true);
	tvh->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(tvh, &QHeaderView::sectionDoubleClicked, this, [&](int index) { sortColumn(index); });
	connect(tvh, &QHeaderView::customContextMenuRequested, this, [&](const QPoint &pos) {
		QMenu menu;
		auto show = menu.addMenu(QString::fromUtf8(obs_module_text("PerfViewer.Columns")));
		auto sort = menu.addMenu(QString::fromUtf8(obs_module_text("PerfViewer.Sort")));
		auto tvh = treeView->header();
		auto a = sort->addAction(QString::fromUtf8(obs_module_text("PerfViewer.None")));
		a->setCheckable(true);
		a->setChecked(proxy->sortColumn() < 0);
		connect(a, &QAction::triggered, [this] {
			auto tvh = treeView->header();
			treeView->sortByColumn(-1, Qt::AscendingOrder);
		});
		for (int i = 0; i < tvh->count(); i++) {
			auto title = model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
			a = show->addAction(title);
			a->setEnabled(i != 0);
			a->setCheckable(true);
			a->setChecked(!tvh->isSectionHidden(i));
			connect(a, &QAction::triggered, [this, i] {
				auto tvh = treeView->header();
				tvh->setSectionHidden(i, !tvh->isSectionHidden(i));
			});
			a = sort->addAction(title);
			a->setCheckable(true);
			a->setChecked(proxy->sortColumn() == i);
			connect(a, &QAction::triggered, [this, i] { sortColumn(i); });
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

PerfTreeModel::PerfTreeModel(QObject *parent) : QAbstractItemModel(parent)
{
	header = {
		QString::fromUtf8(obs_module_text("PerfViewer.Name")),
		QString::fromUtf8(obs_module_text("PerfViewer.Type")),
		QString::fromUtf8(obs_module_text("PerfViewer.Active")),
		QString::fromUtf8(obs_module_text("PerfViewer.Enabled")),
		QString::fromUtf8(obs_module_text("PerfViewer.Tick")),
		QString::fromUtf8(obs_module_text("PerfViewer.Render")),
#ifndef __APPLE__
		QString::fromUtf8(obs_module_text("PerfViewer.RenderGPU")),
#endif
		QString::fromUtf8(obs_module_text("PerfViewer.FPS")),
		QString::fromUtf8(obs_module_text("PerfViewer.RenderedFPS")),
	};

	updater.reset(new QuickThread([this] {
		while (true) {
			QThread::msleep(refreshInterval);
			updateData();
		}
	}));

	updater->start();
}

void OBSPerfViewer::sourceListUpdated()
{
	if (loaded)
		return;

	for (int i = 0; i < PerfTreeModel::Columns::NUM_COLUMNS; i++)
		treeView->resizeColumnToContents(i);
	loaded = true;
}

void OBSPerfViewer::sortColumn(int index)
{
	if (proxy->sortColumn() == index) {
		if (proxy->sortOrder() == Qt::AscendingOrder)
			treeView->sortByColumn(index, Qt::DescendingOrder);
		else
			treeView->sortByColumn(index, Qt::AscendingOrder);
		return;
	}
	auto tvh = treeView->header();
	if (index == PerfTreeModel::NAME || index == PerfTreeModel::TYPE)
		treeView->sortByColumn(index, Qt::AscendingOrder);
	else
		treeView->sortByColumn(index, Qt::DescendingOrder);
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

static double ns_to_ms(uint64_t ns)
{
	return (double)ns / 1000000.0;
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

	auto name = sourceModel()->data(itemIndex, PerfTreeModel::RawDataRole).toString();
	return name.contains(filterRegularExpression());
}

bool PerfViewerProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
	QVariant leftData = sourceModel()->data(left, PerfTreeModel::SortRole);
	QVariant rightData = sourceModel()->data(right, PerfTreeModel::SortRole);

	if (leftData.userType() == QMetaType::Double)
		return leftData.toDouble() < rightData.toDouble();
	if (leftData.userType() == QMetaType::ULongLong)
		return leftData.toULongLong() < rightData.toULongLong();
	if (leftData.userType() == QMetaType::Int)
		return leftData.toInt() < rightData.toInt();
	if (leftData.userType() == QMetaType::QString)
		return QString::localeAwareCompare(leftData.toString(), rightData.toString()) < 0;

	return false;
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
		if (index.column() != PerfTreeModel::ACTIVE && index.column() != PerfTreeModel::ENABLED)
			return {};

		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		auto d = item->data(index.column());
		if (d.userType() == QMetaType::Bool)
			return d.toBool() ? Qt::Checked : Qt::Unchecked;

	} else if (role == Qt::DisplayRole) {
		if (index.column() == PerfTreeModel::ACTIVE || index.column() == PerfTreeModel::ENABLED)
			return {};

		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		return item->data(index.column());

	} else if (role == Qt::DecorationRole) {
		if (index.column() != PerfTreeModel::NAME)
			return {};

		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		return item->getIcon();
	} else if (role == Qt::ForegroundRole) {
		if (index.column() != PerfTreeModel::TICK && index.column() != PerfTreeModel::RENDER
#ifndef __APPLE__
		    && index.column() != PerfTreeModel::RENDER_GPU
#endif
		)
			return {};

		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		return item->getTextColour(index.column());
	} else if (role == SortRole) {
		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		return item->sortData(index.column());
	} else if (role == RawDataRole) {
		auto item = static_cast<PerfTreeItem *>(index.internalPointer());
		return item->rawData(index.column());
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
		return header.at(section);
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
	return (int)header.count();
}

PerfTreeItem::PerfTreeItem(obs_sceneitem_t *sceneitem, PerfTreeItem *parentItem, PerfTreeModel *model)
	: PerfTreeItem(obs_sceneitem_get_source(sceneitem), parentItem, model)

{
	m_sceneitem = sceneitem;
}

PerfTreeItem::PerfTreeItem(obs_source_t *source, PerfTreeItem *parent, PerfTreeModel *model)
	: m_parentItem(parent),
	  m_model(model),
	  m_source(source)
{
	m_perf = new profiler_result_t;
	memset(m_perf, 0, sizeof(profiler_result_t));
}

/* Fake item (e.g. to group private sources) */
PerfTreeItem::PerfTreeItem(QString name, PerfTreeItem *parent, PerfTreeModel *model)
	: m_parentItem(parent),
	  m_model(model),
	  name(std::move(name))
{
	m_perf = nullptr;
}

PerfTreeItem::~PerfTreeItem()
{
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
	return PerfTreeModel::Columns::NUM_COLUMNS;
}

static QString GetSourceName(obs_source_t *source)
{
	const char *name = obs_source_get_name(source);
	return QString::fromUtf8(name ? name : obs_module_text("PerfViewer.NoName"));
}

static QString GetSourceType(obs_source_t *source)
{
	if (!source)
		return QString::fromUtf8("");
	return QString::fromUtf8(obs_source_get_display_name(obs_source_get_unversioned_id(source)));
}

static QString GetTickStr(profiler_result_t *perf)
{
	return QString::asprintf("%.03f / %.03f", ns_to_ms(perf->tick_avg), ns_to_ms(perf->tick_max));
}

static QString GetRenderStr(profiler_result_t *perf)
{
	return QString::asprintf("%.02f / %.02f / %0.02f", ns_to_ms(perf->render_avg), ns_to_ms(perf->render_max),
				 ns_to_ms(perf->render_sum));
}

#ifndef __APPLE__
static QString GetRenderGPUStr(profiler_result_t *perf)
{
	return QString::asprintf("%.02f / %.02f / %0.02f", ns_to_ms(perf->render_gpu_avg), ns_to_ms(perf->render_gpu_max),
				 ns_to_ms(perf->render_gpu_sum));
}
#endif

static QString GetSourceFPS(profiler_result_t *perf, obs_source_t *source)
{
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER &&
	    (obs_source_get_output_flags(source) & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO) {
		return QString::asprintf("%.02f (%.02f ms / %.02f ms)", perf->async_input, ns_to_ms(perf->async_input_best),
					 ns_to_ms(perf->async_input_worst));
	} else {
		return QString::fromUtf8(obs_module_text("PerfViewer.NA"));
	}
}

static QString GetSourceRenderedFPS(profiler_result_t *perf, obs_source_t *source)
{
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER &&
	    (obs_source_get_output_flags(source) & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO) {
		return QString::asprintf("%.02f (%.02f ms / %.02f ms)", perf->async_rendered, ns_to_ms(perf->async_rendered_best),
					 ns_to_ms(perf->async_rendered_worst));
	} else {
		return QString::fromUtf8(obs_module_text("PerfViewer.NA"));
	}
}

QVariant PerfTreeItem::data(int column) const
{
	// Fake items have a name but nothing else
	if (!name.isEmpty()) {
		if (column == 0)
			return name;
		return {};
	}

	// Root item has no data or if perf is null we can't return anything
	if (!m_source || !m_perf)
		return {};

	switch (column) {
	case PerfTreeModel::NAME:
		return GetSourceName(m_source);
	case PerfTreeModel::TYPE:
		return GetSourceType(m_source);
	case PerfTreeModel::ACTIVE:
		return rendered;
	case PerfTreeModel::ENABLED:
		return m_sceneitem ? obs_sceneitem_visible(m_sceneitem) : m_source ? obs_source_enabled(m_source) : false;
	case PerfTreeModel::TICK:
		return GetTickStr(m_perf);
	case PerfTreeModel::RENDER:
		return GetRenderStr(m_perf);
#ifndef __APPLE__
	case PerfTreeModel::RENDER_GPU:
		return GetRenderGPUStr(m_perf);
#endif
	case PerfTreeModel::FPS:
		return GetSourceFPS(m_perf, m_source);
	case PerfTreeModel::FPS_RENDERED:
		return GetSourceRenderedFPS(m_perf, m_source);
	default:
		return {};
	}
}

QVariant PerfTreeItem::rawData(int column) const
{
	if (!name.isEmpty()) {
		if (column == PerfTreeModel::NAME)
			return name;
		return {};
	}

	// Root item has no data or if perf is null we can't return anything
	if (!m_source || !m_perf)
		return {};

	switch (column) {
	case PerfTreeModel::NAME:
		return m_source ? QString::fromUtf8(obs_source_get_name(m_source)) : name;
	case PerfTreeModel::TYPE:
		return GetSourceType(m_source);
	case PerfTreeModel::ACTIVE:
		return rendered;
	case PerfTreeModel::ENABLED:
		return m_sceneitem ? obs_sceneitem_visible(m_sceneitem) : m_source ? obs_source_enabled(m_source) : false;
	case PerfTreeModel::TICK:
		return (qulonglong)m_perf->tick_max;
	case PerfTreeModel::RENDER:
		return (qulonglong)m_perf->render_max;
#ifndef __APPLE__
	case PerfTreeModel::RENDER_GPU:
		return (qulonglong)m_perf->render_gpu_max;
#endif
	case PerfTreeModel::FPS:
		return m_perf->async_input;
	case PerfTreeModel::FPS_RENDERED:
		return m_perf->async_rendered;
	default:
		return {};
	}
}

QVariant PerfTreeItem::sortData(int column) const
{
	return rawData(column);
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
	if (m_source) {
		if (obs_source_get_type(m_source) == OBS_SOURCE_TYPE_FILTER)
			rendered = m_parentItem->isRendered();
		else
			rendered = obs_source_showing(m_source);

		source_profiler_fill_result(m_source, m_perf);

		if (m_model)
			m_model->itemChanged(this);
	}

	if (!m_childItems.empty()) {
		for (auto item : m_childItems)
			item->update();
	}
}

QIcon PerfTreeItem::getIcon() const
{
	// ToDo icons for root?
	if (!m_source)
		return {};
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	const char *id = obs_source_get_id(m_source);

	// Todo filter icon from source toolbar
	if (strcmp(id, "scene") == 0)
		return main_window->property("sceneIcon").value<QIcon>();
	else if (strcmp(id, "group") == 0)
		return main_window->property("groupIcon").value<QIcon>();
	else if (obs_source_get_type(m_source) == OBS_SOURCE_TYPE_FILTER)
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

QVariant PerfTreeItem::getTextColour(int column) const
{
	// root/fake elements don't have performance data
	if (!m_perf)
		return {};

	double target = m_model->targetFrameTime();
	double frameTime;

	switch (column) {
	case PerfTreeModel::TICK:
		frameTime = ns_to_ms(m_perf->tick_max);
		break;
	case PerfTreeModel::RENDER:
		frameTime = ns_to_ms(m_perf->render_max);
		break;
#ifndef __APPLE__
	case PerfTreeModel::RENDER_GPU:
		frameTime = ns_to_ms(m_perf->render_gpu_max);
		break;
#endif
	default:
		return {};
	}

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
