/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 2016 Johannes Ohlemacher (http://github.com/eXistence/fhDOOM)

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "MaterialBrowser.h"

#include "DeclModel.h"

#include <qfilesystemmodel.h>
#include <qtreeview.h>
#include <qsortfilterproxymodel.h>
#include <qlineedit.h>
#include <qdir.h>
#include <qimage.h>
#include <qsplitter.h>
#include <qfileiconprovider.h>
#include <qlistwidget.h>
#include <qprogressdialog.h>
#include "../../renderer/Image.h"
#include "../../renderer/ImageData.h"
#include "../tools/radiant/GLDrawable.h"
#include "../widgets/RenderWidget.h"
#include "../tools/radiant/QE3.H"
#include "../tools/radiant/SELECT.H"


static QByteArray readFile( const char* filename ) {

	void* buffer = nullptr;
	auto len = fileSystem->ReadFile( filename, &buffer, nullptr );
	if (len <= 0 || !buffer)
		return QByteArray();

	QByteArray ret( (const char*)buffer, len );
	fileSystem->FreeFile( buffer );
	return ret;
}

static QByteArray readFile( const QString& filename ) {
	auto tmp = filename.toLatin1();
	return readFile( tmp.data() );
}


fhViewPort::fhViewPort( idGLDrawable* drawable, QWidget* parent /* = nullptr */ )
	: fhDeclPreviewWidget( parent )
	, drawable( drawable )
{
	renderWidget = new fhRenderWidget( this );
	renderWidget->setDrawable( drawable );

	auto layout = new QVBoxLayout();
	layout->addWidget( renderWidget );

	this->setLayout( layout );

	QObject::connect( &timer, &QTimer::timeout, [this](){
		if (this->isVisible())
			renderWidget->updateDrawable();
	} );

	setUpdateRate( 30 );
}


fhViewPort::~fhViewPort() {
	delete drawable;
}

void fhViewPort::setDecl( const char* declname ) {
	drawable->setMedia( declname );
	renderWidget->updateDrawable();
}

void fhViewPort::setUpdateRate( unsigned hz ) {
	timer.stop();

	if (hz > 0) {
		timer.setInterval( 1000 / hz );
		timer.start();
	}
}


fhAbstractContentBrowser::fhAbstractContentBrowser( QWidget* parent )
	: QDialog( parent ) {

}

void fhAbstractContentBrowser::init( QSortFilterProxyModel* proxyModel, fhDeclPreviewWidget* previewWidget) {

	this->previewWidget = previewWidget;
	tree = new QTreeView( this );
	QWidget* left = new QWidget( this );

	tree->setModel( proxyModel );
	tree->setSortingEnabled( true );
	tree->setRootIsDecorated( true );

	QVBoxLayout* treeViewLayout = new QVBoxLayout();
	treeViewLayout->addWidget( tree );

	if (previewWidget) {
		treeViewLayout->addWidget( previewWidget );
	}

	left->setLayout( treeViewLayout );

	list = new QListWidget( this );
	list->setViewMode( QListWidget::IconMode );
	list->setIconSize( QSize( 256, 256 ) );
	list->setResizeMode( QListWidget::Adjust );
	list->setMovement( QListView::Static );
	list->setUniformItemSizes( false );
	list->setDragEnabled( true );
	list->setSelectionMode( QAbstractItemView::SingleSelection );
	list->setBatchSize( 100 );
	list->setLayoutMode( QListView::Batched );

	QSplitter* splitter = new QSplitter( this );
	splitter->addWidget( left );
	splitter->addWidget( list );

	QVBoxLayout* layout = new QVBoxLayout( this );
	layout->addWidget( splitter );
	this->setLayout( layout );

	QObject::connect( tree, &QTreeView::clicked, [this, proxyModel]( const QModelIndex& index ) {
		if (!index.isValid()) {
			return;
		}

		auto src = proxyModel->mapToSource( index );
		onTreeItemSelected( src );
	} );

	QObject::connect( list, &QListWidget::itemSelectionChanged, [this](){
		auto selected = list->selectedItems();
		if (!selected.isEmpty() && selected[0] != nullptr) {
			QByteArray tmp = selected[0]->data( 100 ).toString().toLocal8Bit();

			if (this->previewWidget) {
				this->previewWidget->setDecl( tmp.data() );
			}

			this->onItemSelected( tmp.data() );
		}
	} );
}

fhAbstractContentBrowser::~fhAbstractContentBrowser() {

}

void fhAbstractContentBrowser::onItemSelected(const char* name) {}
QIcon fhAbstractContentBrowser::createIcon( const idDecl* decl ) { return QIcon();  }

void fhAbstractContentBrowser::onTreeItemSelected( const QModelIndex& index ) {

	auto d = static_cast<fhTreeDataItem<idDecl>*>(index.internalPointer());
	if (!d)
		return;

	if (d->type == fhTreeDataItem<idDecl>::Directory) {
		list->clear();

		QProgressDialog progress( "Copying files...", "Abort Copy", 0, d->childs.size(), this );
		progress.setWindowModality( Qt::WindowModal );
		progress.show();
		QApplication::processEvents();

		for (int i=0; i<d->childs.size(); ++i) {
			auto child = d->childs[i];

			if (i % 20 == 0) {
				progress.setValue( i );
				QApplication::processEvents();
			}

			if (child->type != fhTreeDataItem<idDecl>::Decl || !child->get()) {
				continue;
			}

			QListWidgetItem* item = new QListWidgetItem();

			QString name = child->get()->GetName();
			if (!iconCache.contains( name )) {
				iconCache.insert( name, createIcon( child->get() ) );
			}

			item->setIcon( iconCache.value( name ) );
			item->setText( child->name );
			item->setData( 100, QVariant( child->get()->GetName() ) );
			list->addItem( item );
		}

		progress.setValue( d->childs.size() );
	}
	else if (d->type == fhTreeDataItem<idDecl>::Decl && d->get()) {
		if (previewWidget) {
			previewWidget->setDecl( d->get()->GetName() );
		}
	}
}



































enum class fhMaterialDeclColumns {
	EditorImageName = static_cast<int>(fhCommonDeclColumns::COUNT),
	COUNT
};

class fhMaterialModel : public fhDeclModel<idMaterial, DECL_MATERIAL, fhMaterialDeclColumns> {
protected:
	virtual QVariant getDisplayData( const idMaterial& decl, fhMaterialDeclColumns column ) const override {
		switch (column) {
		case fhMaterialDeclColumns::EditorImageName:
			auto material = declManager->FindMaterial( decl.GetName(), true );
			return QVariant( material->GetEditorImage()->imgName.c_str() );
		}
		return QVariant();
	}
};

fhMaterialBrowser::fhMaterialBrowser( QWidget* parent )
	: fhAbstractContentBrowser( parent ) {

	materialModel = new fhMaterialModel();
	materialModel->populate();
	QSortFilterProxyModel *proxyModel = new fhDeclFilterProxyModel<idMaterial>( this );
	proxyModel->setSourceModel( materialModel );

	viewPort = new fhViewPort( new idGLDrawableMaterial(), this );

	init(proxyModel, viewPort);
}

fhMaterialBrowser::~fhMaterialBrowser() {
}

void fhMaterialBrowser::onItemSelected( const char* name ) {
	if (com_editorActive) {
		Select_SetDefaultTexture( declManager->FindMaterial( name ), false, true );
		UpdateSurfaceDialog();
		UpdatePatchInspector();
	}
}

QIcon fhMaterialBrowser::createIcon( const idDecl* decl ) {

	auto material = declManager->FindMaterial( decl->GetName() );
	if (!material) {
		return QIcon();
	}

	auto editorImage = material->GetEditorImage();
	if (!editorImage) {
		return QIcon();
	}

	QString name = editorImage->imgName;
	if (!name.endsWith( ".tga" )) {
		name += ".tga";
	}

	QByteArray blob1 = readFile( name );

	QImage image;
	if (image.loadFromData( blob1, "tga" )) {
		return QIcon( QPixmap::fromImage( image ) );
	}

	return QIcon();
}