
#include "obs-module.h"
#include <QDialog>
#include <QThread>
#include <QTreeView>
#include <QSortFilterProxyModel>
#include <util/source-profiler.h>

class PerfTreeItem;

class PerfTreeColumn {
	QVariant (*m_get_value)(const PerfTreeItem *item);
	QString m_name;
	bool m_is_duration;
	bool m_sort_desc;
	bool m_default_hidden;

public:
	PerfTreeColumn(QString name, QVariant (*getValue)(const PerfTreeItem *item), bool is_duration = false,
		       bool sort_desc = false, bool default_hidden = false);
	QString Name() { return m_name; }
	QVariant Value(const PerfTreeItem *item) { return m_get_value(item); }
	bool DefaultHidden() { return m_default_hidden; }
	friend class PerfTreeModel;
};

class PerfTreeModel;
class PerfViewerProxyModel;

class OBSPerfViewer : public QDialog {
	Q_OBJECT

	PerfTreeModel *model = nullptr;
	PerfViewerProxyModel *proxy = nullptr;

	QTreeView *treeView = nullptr;

	bool loaded = false;

public:
	OBSPerfViewer(QWidget *parent = nullptr);
	~OBSPerfViewer() override;

public slots:
	void sourceListUpdated();
	void sortColumn(int index);
};

class PerfTreeItem;

class PerfTreeModel : public QAbstractItemModel {
	Q_OBJECT

public:
	explicit PerfTreeModel(QObject *parent = nullptr);
	~PerfTreeModel() override;

	QVariant data(const QModelIndex &index, int role) const override;
	Qt::ItemFlags flags(const QModelIndex &index) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex &index) const override;
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;

	void itemChanged(PerfTreeItem *item);

	enum ShowMode { SCENE, SOURCE, FILTER, TRANSITION, ALL };

	void setShowMode(enum ShowMode s = ShowMode::SCENE)
	{
		showMode = s;
		refreshSources();
	}
	enum ShowMode getShowMode() { return showMode; }

	void setRefreshInterval(int interval);

	double targetFrameTime() const { return frameTime; }

	bool sortDefaultDescending(int index)
	{
		auto column = columns.at(index);
		return column.m_sort_desc;
	};

	QList<int> getDefaultHiddenColumns();

public slots:
	void refreshSources();

private slots:
	void updateData();

private:
	PerfTreeItem *rootItem = nullptr;
	QList<PerfTreeColumn> columns;
	std::unique_ptr<QThread> updater;

	enum ShowMode showMode = ShowMode::SCENE;
	bool refreshing = false;
	double frameTime = 0.0;
	int refreshInterval = 1000;
};

class PerfTreeItem {
public:
	explicit PerfTreeItem(obs_sceneitem_t *sceneitem, PerfTreeItem *parentItem = nullptr, PerfTreeModel *model = nullptr);
	explicit PerfTreeItem(obs_source_t *source, PerfTreeItem *parentItem = nullptr, PerfTreeModel *model = nullptr);
	~PerfTreeItem();

	void appendChild(PerfTreeItem *item);
	void prependChild(PerfTreeItem *item);

	PerfTreeItem *child(int row) const;
	int childCount() const;
	int columnCount() const;
	int row() const;
	PerfTreeItem *parentItem();

	PerfTreeModel *model() const { return m_model; }

	void update();
	QIcon getIcon(obs_source_t *source) const;
	QVariant getTextColour(double frameTime) const;

	bool isRendered() const { return rendered; }

private:
	QList<PerfTreeItem *> m_childItems;
	PerfTreeItem *m_parentItem;
	PerfTreeModel *m_model;

	profiler_result_t *m_perf = nullptr;
	obs_weak_source_t *m_source = nullptr;
	obs_sceneitem_t *m_sceneitem = nullptr;
	QString name;
	QString sourceType;
	bool async = false;
	bool rendered = false;
	bool enabled = false;
	QIcon icon;

	friend class PerfTreeModel;
};

class PerfViewerProxyModel : public QSortFilterProxyModel {
	Q_OBJECT

public:
	PerfViewerProxyModel(QObject *parent = nullptr) : QSortFilterProxyModel(parent)
	{
		setRecursiveFilteringEnabled(true);
		setSortRole(Qt::UserRole);
	}

public slots:
	void setFilterText(const QString &filter);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

class QuickThread : public QThread {
public:
	explicit inline QuickThread(std::function<void()> func_) : func(func_) {}

private:
	virtual void run() override { func(); }

	std::function<void()> func;
};
