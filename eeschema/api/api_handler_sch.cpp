/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 Jon Evans <jon@craftyjon.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <api/api_handler_sch.h>
#include <sch_symbol.h>
#include <sch_line.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <api/api_sch_utils.h>
#include <api/schematic/schematic_commands.pb.h>
#include <api/schematic/schematic_types.pb.h>
#include <connection_graph.h>
#include <schematic.h>
#include <sch_sheet_path.h>
#include <api/api_enums.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <tools/sch_selection_tool.h>
#include <api/api_utils.h>
#include <magic_enum.hpp>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <wx/filename.h>

#include <api/common/types/base_types.pb.h>

using namespace kiapi::common::commands;
using kiapi::common::types::CommandStatus;
using kiapi::common::types::DocumentType;
using kiapi::common::types::ItemRequestStatus;
using google::protobuf::Empty;


API_HANDLER_SCH::API_HANDLER_SCH( SCH_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame ),
        m_frame( aFrame )
{
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_SCH::handleGetOpenDocuments );
    registerHandler<GetItems, GetItemsResponse>(
            &API_HANDLER_SCH::handleGetItems );
    registerHandler<GetSelection, SelectionResponse>(
            &API_HANDLER_SCH::handleGetSelection );
    registerHandler<AddToSelection, SelectionResponse>(
            &API_HANDLER_SCH::handleAddToSelection );
    registerHandler<RemoveFromSelection, SelectionResponse>(
            &API_HANDLER_SCH::handleRemoveFromSelection );
    registerHandler<ClearSelection, Empty>(
            &API_HANDLER_SCH::handleClearSelection );
    registerHandler<RunAction, RunActionResponse>(
            &API_HANDLER_SCH::handleRunAction );
    registerHandler<SaveDocument, Empty>(
            &API_HANDLER_SCH::handleSaveDocument );
    registerHandler<RevertDocument, Empty>(
            &API_HANDLER_SCH::handleRevertDocument );
    registerHandler<SaveDocumentToString, SavedDocumentResponse>(
            &API_HANDLER_SCH::handleSaveDocumentToString );

    using namespace kiapi::schematic::commands;
    registerHandler<GetNets, GetNetsResponse>(
            &API_HANDLER_SCH::handleGetNets );
    registerHandler<GetSheetHierarchy, GetSheetHierarchyResponse>(
            &API_HANDLER_SCH::handleGetSheetHierarchy );
}


std::unique_ptr<COMMIT> API_HANDLER_SCH::createCommit()
{
    return std::make_unique<SCH_COMMIT>( m_frame );
}


bool API_HANDLER_SCH::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_SCHEMATIC )
        return false;

    // TODO(JE) need serdes for SCH_SHEET_PATH <> SheetPath
    return true;

    //wxString currentPath = m_frame->GetCurrentSheet().PathAsString();
    //return 0 == aDocument.sheet_path().compare( currentPath.ToStdString() );
}


HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_SCH::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_SCHEMATIC )
    {
        ApiResponseStatus e;

        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse response;
    common::types::DocumentSpecifier doc;

    wxFileName fn( m_frame->GetCurrentFileName() );

    doc.set_type( DocumentType::DOCTYPE_SCHEMATIC );
    doc.set_board_filename( fn.GetFullName() );

    response.mutable_documents()->Add( std::move( doc ) );
    return response;
}


HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> API_HANDLER_SCH::createItemForType( KICAD_T aType,
        EDA_ITEM* aContainer )
{
    if( !aContainer )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tried to create an item in a null container" );
        return tl::unexpected( e );
    }

    if( aType == SCH_PIN_T && !dynamic_cast<SCH_SYMBOL*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a pin in {}, which is not a symbol",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }
    else if( aType == SCH_SYMBOL_T && !dynamic_cast<SCHEMATIC*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a symbol in {}, which is not a "
                                          "schematic",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }

    std::unique_ptr<EDA_ITEM> created = CreateItemForType( aType, aContainer );

    if( !created )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create an item of type {}, which is unhandled",
                                          magic_enum::enum_name( aType ) ) );
        return tl::unexpected( e );
    }

    return created;
}


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_SCH::handleCreateUpdateItemsInternal( bool aCreate,
        const std::string& aClientName,
        const types::ItemHeader &aHeader,
        const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
        std::function<void( ItemStatus, google::protobuf::Any )> aItemHandler )
{
    ApiResponseStatus e;

    auto containerResult = validateItemHeaderDocument( aHeader );

    if( !containerResult && containerResult.error().status() == ApiStatusCode::AS_UNHANDLED )
    {
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }
    else if( !containerResult )
    {
        e.CopyFrom( containerResult.error() );
        return tl::unexpected( e );
    }

    SCH_SCREEN* screen = m_frame->GetScreen();
    EE_RTREE& screenItems = screen->Items();

    std::map<KIID, EDA_ITEM*> itemUuidMap;

    std::for_each( screenItems.begin(), screenItems.end(),
                   [&]( EDA_ITEM* aItem )
                   {
                       itemUuidMap[aItem->m_Uuid] = aItem;
                   } );

    EDA_ITEM* container = static_cast<EDA_ITEM*>( &m_frame->Schematic() );

    if( containerResult->has_value() )
    {
        const KIID& containerId = **containerResult;

        if( itemUuidMap.count( containerId ) )
        {
            container = itemUuidMap.at( containerId );

            if( !container )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format(
                        "The requested container {} is not a valid schematic item container",
                        containerId.AsStdString() ) );
                return tl::unexpected( e );
            }
        }
        else
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format(
                    "The requested container {} does not exist in this document",
                    containerId.AsStdString() ) );
            return tl::unexpected( e );
        }
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    for( const google::protobuf::Any& anyItem : aItems )
    {
        ItemStatus status;
        std::optional<KICAD_T> type = TypeNameFromAny( anyItem );

        if( !type )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( fmt::format( "Could not decode a valid type from {}",
                                                   anyItem.type_url() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        // For symbol creation, use the parametrized constructor (same as interactive placement)
        // instead of default-construct + Deserialize, which leaves m_part null and clobbers fields.
        if( aCreate && *type == SCH_SYMBOL_T )
        {
            kiapi::schematic::types::SchematicSymbol symMsg;

            if( !anyItem.UnpackTo( &symMsg ) )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( "could not unpack SchematicSymbol from request" );
                return tl::unexpected( e );
            }

            LIB_ID libId( symMsg.library_identifier().library_nickname(),
                          symMsg.library_identifier().entry_name() );

            if( !libId.IsValid() )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
                status.set_error_message( "invalid library identifier" );
                aItemHandler( status, anyItem );
                continue;
            }

            // Load library symbol — same path as interactive placement
            SYMBOL_LIBRARY_ADAPTER* adapter =
                    PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );
            LIB_SYMBOL* libSymbol = nullptr;

            if( adapter )
            {
                try
                {
                    libSymbol = adapter->LoadSymbol( libId );
                }
                catch( const IO_ERROR& )
                {
                    libSymbol = nullptr;
                }
            }

            if( !libSymbol )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
                status.set_error_message( fmt::format( "library symbol '{}' not found",
                                                       std::string( libId.Format() ) ) );
                aItemHandler( status, anyItem );
                continue;
            }

            // Extract position
            VECTOR2I pos = kiapi::common::UnpackVector2( symMsg.position() );

            // Extract unit and body style
            int unit = symMsg.unit() > 0 ? symMsg.unit() : 1;
            int bodyStyle = symMsg.body_style();

            // Construct symbol the same way as interactive placement
            auto symbol = std::make_unique<SCH_SYMBOL>( *libSymbol, libId, nullptr,
                                                        unit, bodyStyle, pos );

            // Apply orientation/mirror from proto
            int orient = SYM_ORIENT_0;
            switch( symMsg.orientation() )
            {
            case 1: orient = SYM_ORIENT_90;  break;
            case 2: orient = SYM_ORIENT_180; break;
            case 3: orient = SYM_ORIENT_270; break;
            default: orient = SYM_ORIENT_0;  break;
            }
            if( symMsg.mirror_x() ) orient |= SYM_MIRROR_X;
            if( symMsg.mirror_y() ) orient |= SYM_MIRROR_Y;
            symbol->SetOrientation( orient );

            // Apply field overrides from proto
            for( const auto& f : symMsg.fields() )
            {
                wxString fieldName = wxString::FromUTF8( f.name() );
                wxString fieldText = wxString::FromUTF8( f.text() );

                if( fieldName == wxT( "Reference" ) )
                    symbol->GetField( FIELD_T::REFERENCE )->SetText( fieldText );
                else if( fieldName == wxT( "Value" ) )
                    symbol->GetField( FIELD_T::VALUE )->SetText( fieldText );
                else if( fieldName == wxT( "Footprint" ) )
                    symbol->GetField( FIELD_T::FOOTPRINT )->SetText( fieldText );
                else if( fieldName == wxT( "Datasheet" ) )
                    symbol->GetField( FIELD_T::DATASHEET )->SetText( fieldText );
                else
                {
                    SCH_FIELD* existing = symbol->GetField( fieldName );

                    if( existing )
                        existing->SetText( fieldText );
                    else
                    {
                        SCH_FIELD* newField = symbol->AddField(
                                SCH_FIELD( symbol.get(), FIELD_T::USER, fieldName ) );
                        newField->SetText( fieldText );
                    }
                }
            }

            // Apply exclusion flags
            symbol->SetExcludedFromBOM( symMsg.exclude_from_bom() );
            symbol->SetExcludedFromBoard( symMsg.exclude_from_board() );
            symbol->SetDNP( symMsg.dnp() );

            // Add flattened lib symbol to screen cache (needed for file save)
            std::unique_ptr<LIB_SYMBOL> flatSymbol = libSymbol->Flatten();
            flatSymbol->SetParent();
            screen->AddLibSymbol( new LIB_SYMBOL( *flatSymbol ) );

            // Serialize result back and commit
            google::protobuf::Any newItem;
            status.set_code( ItemStatusCode::ISC_OK );
            symbol->Serialize( newItem );
            commit->Add( symbol.release(), screen );

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Added items via API" ) );

            aItemHandler( status, newItem );
            continue;
        }

        // For wire/bus/line creation, use parametrized constructor with correct layer.
        // Default SCH_LINE() uses LAYER_NOTES which sets dangling=true and wrong render state.
        if( aCreate && *type == SCH_LINE_T )
        {
            kiapi::schematic::types::Line lineMsg;

            if( !anyItem.UnpackTo( &lineMsg ) )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( "could not unpack Line from request" );
                return tl::unexpected( e );
            }

            // Map proto layer to KiCad layer
            SCH_LAYER_ID layer = FromProtoEnum<SCH_LAYER_ID,
                    kiapi::schematic::types::SchematicLayer>( lineMsg.layer() );

            if( layer != LAYER_WIRE && layer != LAYER_BUS && layer != LAYER_NOTES )
                layer = LAYER_WIRE;

            VECTOR2I start = kiapi::common::UnpackVector2( lineMsg.start() );
            VECTOR2I end = kiapi::common::UnpackVector2( lineMsg.end() );

            // Use parametrized constructor — sets correct layer, dangling flags, and width
            auto line = std::make_unique<SCH_LINE>( start, layer );
            line->SetEndPoint( end );

            google::protobuf::Any newItem;
            status.set_code( ItemStatusCode::ISC_OK );
            line->Serialize( newItem );
            commit->Add( line.release(), screen );

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Added items via API" ) );

            aItemHandler( status, newItem );
            continue;
        }

        // For all other item types: default-construct + Deserialize
        HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> creationResult =
                createItemForType( *type, container );

        if( !creationResult )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( creationResult.error().error_message() );
            aItemHandler( status, anyItem );
            continue;
        }

        std::unique_ptr<EDA_ITEM> item( std::move( *creationResult ) );

        if( !item->Deserialize( anyItem ) )
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "could not unpack {} from request",
                                              item->GetClass().ToStdString() ) );
            return tl::unexpected( e );
        }

        if( aCreate && itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_EXISTING );
            status.set_error_message( fmt::format( "an item with UUID {} already exists",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }
        else if( !aCreate && !itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_NONEXISTENT );
            status.set_error_message( fmt::format( "an item with UUID {} does not exist",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        status.set_code( ItemStatusCode::ISC_OK );
        google::protobuf::Any newItem;

        if( aCreate )
        {
            item->Serialize( newItem );
            commit->Add( item.release(), screen );

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Added items via API" ) );
        }
        else
        {
            EDA_ITEM* edaItem = itemUuidMap[item->m_Uuid];

            if( SCH_ITEM* schItem = dynamic_cast<SCH_ITEM*>( edaItem ) )
            {
                if( schItem->Type() == SCH_SYMBOL_T )
                {
                    // For symbols, apply updates field-by-field
                    // to preserve library graphics data
                    SCH_SYMBOL* origSym = static_cast<SCH_SYMBOL*>( schItem );
                    SCH_SYMBOL* newSym = static_cast<SCH_SYMBOL*>( item.get() );

                    commit->Modify( origSym, screen );

                    origSym->SetPosition( newSym->GetPosition() );
                    origSym->SetOrientation( newSym->GetOrientation() );
                    origSym->SetUnit( newSym->GetUnit() );
                    origSym->SetBodyStyle( newSym->GetBodyStyle() );

                    // Update fields from the new item, matching by name
                    for( SCH_FIELD& newField : newSym->GetFields() )
                    {
                        SCH_FIELD* origField = origSym->GetField( newField.GetName() );

                        if( origField )
                            origField->SetText( newField.GetText() );
                    }

                    origSym->Serialize( newItem );
                }
                else
                {
                    schItem->SwapItemData( static_cast<SCH_ITEM*>( item.get() ) );
                    schItem->Serialize( newItem );
                    commit->Modify( schItem, screen );
                }
            }
            else
            {
                wxASSERT( false );
            }

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Created items via API" ) );
        }

        aItemHandler( status, newItem );
    }


    return ItemRequestStatus::IRS_OK;
}


void API_HANDLER_SCH::deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                                           const std::string& aClientName )
{
    SCH_SCREEN* screen = m_frame->GetScreen();

    std::map<KIID, SCH_ITEM*> itemMap;

    for( SCH_ITEM* item : screen->Items() )
        itemMap[item->m_Uuid] = item;

    std::vector<SCH_ITEM*> validatedItems;

    for( auto& [id, status] : aItemsToDelete )
    {
        auto it = itemMap.find( id );

        if( it != itemMap.end() )
        {
            validatedItems.push_back( it->second );
            status = ItemDeletionStatus::IDS_OK;
        }
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    for( SCH_ITEM* item : validatedItems )
        commit->Remove( item, screen );

    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, _( "Deleted items via API" ) );
}


std::optional<EDA_ITEM*> API_HANDLER_SCH::getItemFromDocument( const DocumentSpecifier& aDocument,
                                                               const KIID& aId )
{
    if( !validateDocument( aDocument ) )
        return std::nullopt;

    SCH_SCREEN* screen = m_frame->GetScreen();

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->m_Uuid == aId )
            return item;
    }

    return std::nullopt;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_SCH::handleGetItems(
        const HANDLER_CONTEXT<GetItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetItemsResponse response;

    SCH_SCREEN* screen = m_frame->GetScreen();
    std::set<KICAD_T> typesRequested;

    for( int typeRaw : aCtx.Request.types() )
    {
        auto typeMessage = static_cast<common::types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type != TYPE_NOT_INIT )
            typesRequested.emplace( type );
    }

    for( SCH_ITEM* item : screen->Items() )
    {
        if( !typesRequested.empty() && !typesRequested.count( item->Type() ) )
            continue;

        

        item->Serialize( *response.mutable_items()->Add() );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_SCH::handleGetSelection(
        const HANDLER_CONTEXT<GetSelection>& aCtx )
{
    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    std::set<KICAD_T> filter;

    for( int typeRaw : aCtx.Request.types() )
    {
        auto typeMessage = static_cast<common::types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type != TYPE_NOT_INIT )
            filter.insert( type );
    }

    TOOL_MANAGER* mgr = m_frame->GetToolManager();
    SCH_SELECTION_TOOL* selTool = mgr->GetTool<SCH_SELECTION_TOOL>();

    SelectionResponse response;

    for( EDA_ITEM* item : selTool->GetSelection() )
    {
        if( !filter.empty() && !filter.count( item->Type() ) )
            continue;

        

        item->Serialize( *response.mutable_items()->Add() );
    }

    return response;
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_SCH::handleAddToSelection(
        const HANDLER_CONTEXT<AddToSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    SCH_SCREEN* screen = m_frame->GetScreen();
    TOOL_MANAGER* mgr = m_frame->GetToolManager();
    SCH_SELECTION_TOOL* selTool = mgr->GetTool<SCH_SELECTION_TOOL>();

    std::map<KIID, EDA_ITEM*> itemMap;

    for( SCH_ITEM* item : screen->Items() )
        itemMap[item->m_Uuid] = item;

    std::vector<EDA_ITEM*> toAdd;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        auto it = itemMap.find( KIID( id.value() ) );

        if( it != itemMap.end() )
            toAdd.emplace_back( it->second );
    }

    selTool->AddItemsToSel( &toAdd );
    m_frame->GetCanvas()->Refresh();

    SelectionResponse response;

    for( EDA_ITEM* item : selTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_SCH::handleRemoveFromSelection(
        const HANDLER_CONTEXT<RemoveFromSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    SCH_SCREEN* screen = m_frame->GetScreen();
    TOOL_MANAGER* mgr = m_frame->GetToolManager();
    SCH_SELECTION_TOOL* selTool = mgr->GetTool<SCH_SELECTION_TOOL>();

    std::map<KIID, EDA_ITEM*> itemMap;

    for( SCH_ITEM* item : screen->Items() )
        itemMap[item->m_Uuid] = item;

    std::vector<EDA_ITEM*> toRemove;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        auto it = itemMap.find( KIID( id.value() ) );

        if( it != itemMap.end() )
            toRemove.emplace_back( it->second );
    }

    selTool->RemoveItemsFromSel( &toRemove );
    m_frame->GetCanvas()->Refresh();

    SelectionResponse response;

    for( EDA_ITEM* item : selTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleClearSelection(
        const HANDLER_CONTEXT<ClearSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = m_frame->GetToolManager();
    mgr->RunAction( ACTIONS::selectionClear );

    return Empty();
}


HANDLER_RESULT<RunActionResponse> API_HANDLER_SCH::handleRunAction(
        const HANDLER_CONTEXT<RunAction>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    RunActionResponse response;

    if( m_frame->GetToolManager()->RunAction( aCtx.Request.action(), true ) )
        response.set_status( RunActionStatus::RAS_OK );
    else
        response.set_status( RunActionStatus::RAS_INVALID );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSaveDocument(
        const HANDLER_CONTEXT<SaveDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    m_frame->SaveProject();
    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleRevertDocument(
        const HANDLER_CONTEXT<RevertDocument>& aCtx )
{
    wxFileName fn = m_frame->Schematic().GetFileName();

    if( fn.FileExists() )
    {
        m_frame->GetScreen()->SetContentModified( false );
        m_frame->OpenProjectFiles( { fn.GetFullPath() } );
        m_frame->GetCanvas()->Refresh();
    }

    return Empty();
}


HANDLER_RESULT<SavedDocumentResponse> API_HANDLER_SCH::handleSaveDocumentToString(
        const HANDLER_CONTEXT<SaveDocumentToString>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SavedDocumentResponse response;
    response.mutable_document()->CopyFrom( aCtx.Request.document() );

    wxFileName fn = m_frame->Schematic().GetFileName();
    wxString contents;

    if( fn.FileExists() )
    {
        wxFile file( fn.GetFullPath() );

        if( file.IsOpened() )
            file.ReadAll( &contents );
    }

    response.set_contents( contents.ToUTF8() );
    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetNetsResponse> API_HANDLER_SCH::handleGetNets(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetNets>& aCtx )
{
    kiapi::schematic::commands::GetNetsResponse response;

    CONNECTION_GRAPH* graph = m_frame->Schematic().ConnectionGraph();

    if( graph )
    {
        for( const auto& [key, subgraphs] : graph->GetNetMap() )
        {
            if( subgraphs.empty() )
                continue;

            auto* net = response.add_nets();
            net->set_name( key.Name.ToStdString() );
            net->set_code( key.Netcode );
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetSheetHierarchyResponse>
API_HANDLER_SCH::handleGetSheetHierarchy(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetSheetHierarchy>& aCtx )
{
    kiapi::schematic::commands::GetSheetHierarchyResponse response;

    SCH_SHEET_LIST hierarchy = m_frame->Schematic().Hierarchy();

    for( const SCH_SHEET_PATH& path : hierarchy )
    {
        auto* sheet = response.add_sheets();
        sheet->set_path( path.PathAsString().ToStdString() );
        sheet->set_name( path.Last()->GetName().ToStdString() );
        sheet->set_filename( path.Last()->GetFileName().ToStdString() );
        sheet->set_page( path.GetPageNumber().ToStdString() );
        sheet->mutable_id()->set_value( path.Last()->m_Uuid.AsStdString() );
    }

    return response;
}
