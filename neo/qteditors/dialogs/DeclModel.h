#pragma once

#include <qabstractitemmodel.h>
#include <qsortfilterproxymodel.h>
#include <qfileiconprovider.h>
#include "../framework/DeclManager.h"


template<typename T>
class optional final
{
public:
	optional()
		: state(0)
	{}

	optional( const T& t )
		: state( 1 ) {
		new (&buffer[0]) T( t );
	}

	optional( T&& t )
		: state( 1 ) {
		new (&buffer[0]) T( std::move(t) );
	}

	optional( const optional& rhs )
		: state(rhs.state) {
		if (state) {
			new (&buffer[0]) T( *rhs );
		}
	}

	optional( optional&& rhs )
		: state( rhs.state ) {
		if (state) {
			new (&buffer[0]) T( std::move(*rhs) );
		}
	}

	~optional() {
		reset();
	}

	operator bool() const {
		return hasValue();
	}

	T& operator->() {
		assert( hasValue() );
		return *reinterpret_cast<T*>(&buffer[0]);
	}

	const T& operator->() const {
		assert( hasValue() );
		return *reinterpret_cast<const T*>(&buffer[0]);
	}

	T& get() {
		assert( hasValue() );
		T* tmp = reinterpret_cast<T*>(&buffer[0]);
		return *tmp;
	}

	const T& get() const {
		assert( hasValue() );
		const T* tmp = reinterpret_cast<const T*>(&buffer[0]);
		return *tmp;
	}

	void operator=(const T& t) {
		set( t );
	}

	void operator=(const optional& rhs) {
		reset();
		if (rhs) {
			(*this) = *rhs;
		}
	}

	void operator=(optional&& rhs) {
		reset();
		if (rhs) {
			(*this) = std::move(*rhs);
		}
	}

	void operator=(T&& t) {
		set( std::move(t) );
	}

	void reset() {
		if (state) {
			reinterpret_cast<T*>(&buffer[0])->~T();
		}
	}

	void set( const T& t ) {
		reset();
		new (&buffer[0]) T( t );
		state = 1;
	}

	void set( T&& t ) {
		reset();
		new (&buffer[0]) T( std::move(t) );
		state = 1;
	}

	bool hasValue() const {
		return state != 0;
	}

private:
	int64 state;
	char buffer[sizeof( T )];
};


enum class fhCommonDeclColumns {
	Name = 0,
	Filename,
	COUNT
};

template<typename TDataType>
struct fhTreeDataItem {
	using ThisType = fhTreeDataItem<TDataType>;

	enum Type {
		Directory,
		Decl
	};

	fhTreeDataItem()
		: type( Directory )
		, parent( nullptr )
		, data( nullptr ) {
	}

	const TDataType* get() const {
		return data.get();
	}

	void addChild( const TDataType* data, const QString& path ) {
		const auto i = path.indexOf( "/" );

		if (i == 0) {
			addChild( data, path.mid( 1 ) );
		}
		else if (i < 0) {
			ThisType* entry = new ThisType;
			entry->type = Decl;
			entry->name = path;
			entry->parent = this;
			entry->data = data;
			childs.push_back( entry );
		}
		else {
			const auto name = path.left( i );

			ThisType* entry = nullptr;

			for (auto& child : childs) {
				if (child->type == Directory && child->name == name) {
					entry = child;
					break;
				}
			}

			if (!entry) {
				entry = new ThisType;
				entry->type = Directory;
				entry->name = name;
				entry->parent = this;
				entry->data = nullptr;
				childs.push_back( entry );
			}

			entry->addChild( data, path.mid( i + 1 ) );
		}
	}

	Type type;
	QString name;

	ThisType* parent;
	QVector<ThisType*> childs;
	optional<const TDataType*> data;
};


template<typename TDataType, typename TDeclColumns = fhCommonDeclColumns>
class fhTreeDataModel : public QAbstractItemModel {

public:
	using Item = fhTreeDataItem<TDataType>;

	virtual QModelIndex index( int row, int column, const QModelIndex &parent ) const override {
		if (!hasIndex( row, column, parent )) {
			return QModelIndex();
		}

		Item* p = parent.isValid() ? getItem( parent ) : const_cast<Item*>(&root);

		if (auto c = p->childs[row]) {
			return createIndex( row, column, c );
		}

		return QModelIndex();
	}

	virtual QModelIndex parent( const QModelIndex &index ) const override {
		if (!index.isValid()) {
			return QModelIndex();
		}

		Item* child = getItem( index );
		Item* parent = child->parent;

		if (parent == &root) {
			return QModelIndex();
		}

		return createIndex( parent->parent->childs.indexOf( parent ), 0, parent );
	}

	virtual int rowCount( const QModelIndex &parent ) const override {
		if (parent.column() > 0) {
			return 0;
		}

		const Item* p = parent.isValid() ? getItem( parent ) : &root;

		return p->childs.size();
	}

	virtual int columnCount( const QModelIndex &parent ) const override {
		return static_cast<int>(TDeclColumns::COUNT);
	}

	virtual QVariant headerData( int section, Qt::Orientation orientation, int role ) const override {
		if (role != Qt::DisplayRole) {
			return QVariant();
		}

		switch (static_cast<fhCommonDeclColumns>(section)) {
		case fhCommonDeclColumns::Name:
			return "Name";
		case fhCommonDeclColumns::Filename:
			return "File";
		default:
			return getHeaderData( static_cast<TDeclColumns>(section) );
		}
	}

	virtual QVariant data( const QModelIndex &index, int role ) const override {
		QFileIconProvider iconProvider;
		if (auto entry = getItem( index )) {
			if (role == Qt::SizeHintRole) {
				return QSize( 24, 24 );
			}
			if (role == Qt::DisplayRole) {
				switch (static_cast<fhCommonDeclColumns>(index.column())) {
				case fhCommonDeclColumns::Name:
					return entry->name;
				case fhCommonDeclColumns::Filename:
					if (entry->get()) {
						return entry->get()->GetFileName();
					}
					break;
				default:
					if (entry->get()) {
						return getDisplayData( *entry->get(),
							static_cast<TDeclColumns>(index.column()) );
					}
				}
			}
			else if (role == Qt::DecorationRole) {
				switch (static_cast<fhCommonDeclColumns>(index.column())) {
				case fhCommonDeclColumns::Name:
					return iconProvider.icon( entry->type == Item::Directory ? QFileIconProvider::Folder : QFileIconProvider::File );
				default:
					break;
				}
			}
		}
		return QVariant();
	}

protected:

	virtual QVariant getDisplayData( const TDataType& decl, TDeclColumns column ) const {
		return QVariant();
	}
	virtual QVariant getDecorationData( const TDataType& decl, TDeclColumns column ) const {
		return QVariant();
	}
	virtual QVariant getHeaderData( TDeclColumns column ) const {
		return QVariant();
	}

	static Item* getItem( const QModelIndex& index ) {
		if (index.isValid())
			return static_cast<Item*>(index.internalPointer());

		return nullptr;
	}

	Item root;
};

template<typename TDataType>
class fhDeclFilterProxyModel : public QSortFilterProxyModel {
public:
	using Entry = fhTreeDataItem<TDataType>;

	explicit fhDeclFilterProxyModel( QObject* parent = nullptr )
		: QSortFilterProxyModel( parent )
	{}

protected:
	virtual bool filterAcceptsRow( int source_row, const QModelIndex& parent ) const override {
		if (auto entry = getEntry( parent )) {
			if (auto c = entry->childs[source_row]) {
				return c->type == Entry::Directory;
			}
		}

		return QSortFilterProxyModel::filterAcceptsRow( source_row, parent );
	}

	virtual bool lessThan( const QModelIndex &left, const QModelIndex &right ) const override {
		auto l = getEntry( left );
		auto r = getEntry( right );

		if (!l || !r) {
			return QSortFilterProxyModel::lessThan( left, right );
		}

		if (l->type != r->type) {
			return l->type < r->type;
		}

		return l->name < r->name;
	}

	static Entry* getEntry( const QModelIndex& index ) {
		if (index.isValid())
			return static_cast<Entry*>(index.internalPointer());

		return nullptr;
	}
};





template<typename TDeclType, int TDeclEnum, typename TDeclColumns = fhCommonDeclColumns>
class fhDeclModel : public fhTreeDataModel<TDeclType, TDeclColumns> {

public:
	void populate( bool smart = false ) {
		int num = declManager->GetNumDecls( static_cast<declType_t>(TDeclEnum) );

		for (int i = 0; i < num; ++i) {
			auto d = declManager->DeclByIndex( static_cast<declType_t>(TDeclEnum), i, false );

			if (auto p = dynamic_cast<const TDeclType*>(d)) {
				QString name = p->GetName();
				root.addChild( p, name );
			}
		}
#if 0
		if (smart) {
			smartDecls( &root );
		}
#endif
	}

private:
#if 0
	void smartDecls( Item* parent ) {
		QVector<Item*> removedFromParent;
		QVector<Item*> addToParent;

		for (int i = 0; i < parent->childCount(); ++i) {
			auto e = parent->child(i);

			int index = e->name.indexOf( "_" );
			if (index < 0 || index + 1 == e->name.size()) {
				continue;
			}

			if (removedFromParent.contains( e )) {
				continue;
			}

			const QStringRef prefix = e->name.leftRef( index + 1 );

			QVector<Item*> entries;
			entries.append( e );

			for (int j = i + 1; j < parent->childCount(); ++j) {
				if (parent->child(j)->name.startsWith( prefix )) {
					entries.append( parent->child(j) );
				}
			}

			if (entries.size() > 1) {
				Item* smartDir = new Item();
				smartDir->type = Item::Directory;
				smartDir->name = prefix.left( prefix.size() - 1 ).toString();
				smartDir->addChild( entries );

				for (auto c : entries) {
					c->name = c->name.mid( prefix.size() );
					c->parent = smartDir;
				}

				addToParent.append( smartDir );
				removedFromParent.append( entries );
			}
		}

		for (auto c : removedFromParent) {
			parent->childs.removeAll( c );
		}

		parent->childs.append( addToParent );

		for (auto c : parent->childs) {
			smartDecls( c );
		}
	}
#endif
};