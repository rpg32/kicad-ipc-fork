/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 Jon Evans <jon@craftyjon.com>
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

#include <magic_enum.hpp>
#include <properties/property.h>

#include <common.h>
#include <api/api_handler_pcb.h>
#include <api/api_pcb_utils.h>
#include <api/api_enums.h>
#include <api/api_utils.h>
#include <board_commit.h>
#include <connectivity/connectivity_data.h>
#include <tool/actions.h>
#include <drc/drc_engine.h>
#include <drc/drc_rule.h>
#include <geometry/shape.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <kicad_clipboard.h>
#include <netinfo.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <view/view.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcb_group.h>
#include <pcb_reference_image.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <pcb_track.h>
#include <pcbnew_id.h>
#include <pcb_marker.h>
#include <drc/drc_item.h>
#include <layer_ids.h>
#include <project.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <zone.h>
#include <router/pns_router.h>
#include <router/router_tool.h>
#include <router/pns_kicad_iface.h>
#include <router/pns_routing_settings.h>
#include <router/pns_sizes_settings.h>
#include <router/pns_itemset.h>
#include <router/pns_item.h>
#include <router/pns_solid.h>
#include <router/pns_segment.h>
#include <router/pns_node.h>
#include <router/pns_line.h>
#include <router/pns_placement_algo.h>
#include <geometry/shape_line_chain.h>
#include <pcbnew_settings.h>
#include <class_draw_panel_gal.h>

#include <api/common/types/base_types.pb.h>
#include <widgets/appearance_controls.h>
#include <widgets/report_severity.h>

using namespace kiapi::common::commands;
using types::CommandStatus;
using types::DocumentType;
using types::ItemRequestStatus;


API_HANDLER_PCB::API_HANDLER_PCB( PCB_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
    registerHandler<RunAction, RunActionResponse>( &API_HANDLER_PCB::handleRunAction );
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_PCB::handleGetOpenDocuments );
    registerHandler<SaveDocument, Empty>( &API_HANDLER_PCB::handleSaveDocument );
    registerHandler<SaveCopyOfDocument, Empty>( &API_HANDLER_PCB::handleSaveCopyOfDocument );
    registerHandler<RevertDocument, Empty>( &API_HANDLER_PCB::handleRevertDocument );

    registerHandler<GetItems, GetItemsResponse>( &API_HANDLER_PCB::handleGetItems );
    registerHandler<GetItemsById, GetItemsResponse>( &API_HANDLER_PCB::handleGetItemsById );

    registerHandler<GetSelection, SelectionResponse>( &API_HANDLER_PCB::handleGetSelection );
    registerHandler<ClearSelection, Empty>( &API_HANDLER_PCB::handleClearSelection );
    registerHandler<AddToSelection, SelectionResponse>( &API_HANDLER_PCB::handleAddToSelection );
    registerHandler<RemoveFromSelection, SelectionResponse>(
            &API_HANDLER_PCB::handleRemoveFromSelection );

    registerHandler<GetBoardStackup, BoardStackupResponse>( &API_HANDLER_PCB::handleGetStackup );
    registerHandler<GetBoardEnabledLayers, BoardEnabledLayersResponse>(
        &API_HANDLER_PCB::handleGetBoardEnabledLayers );
    registerHandler<SetBoardEnabledLayers, BoardEnabledLayersResponse>(
        &API_HANDLER_PCB::handleSetBoardEnabledLayers );
    registerHandler<GetGraphicsDefaults, GraphicsDefaultsResponse>(
            &API_HANDLER_PCB::handleGetGraphicsDefaults );
    registerHandler<GetBoundingBox, GetBoundingBoxResponse>(
            &API_HANDLER_PCB::handleGetBoundingBox );
    registerHandler<GetPadShapeAsPolygon, PadShapeAsPolygonResponse>(
            &API_HANDLER_PCB::handleGetPadShapeAsPolygon );
    registerHandler<CheckPadstackPresenceOnLayers, PadstackPresenceResponse>(
            &API_HANDLER_PCB::handleCheckPadstackPresenceOnLayers );
    registerHandler<GetTitleBlockInfo, types::TitleBlockInfo>(
            &API_HANDLER_PCB::handleGetTitleBlockInfo );
    registerHandler<ExpandTextVariables, ExpandTextVariablesResponse>(
            &API_HANDLER_PCB::handleExpandTextVariables );
    registerHandler<GetBoardOrigin, types::Vector2>( &API_HANDLER_PCB::handleGetBoardOrigin );
    registerHandler<SetBoardOrigin, Empty>( &API_HANDLER_PCB::handleSetBoardOrigin );
    registerHandler<GetBoardLayerName, BoardLayerNameResponse>( &API_HANDLER_PCB::handleGetBoardLayerName );

    registerHandler<InteractiveMoveItems, Empty>( &API_HANDLER_PCB::handleInteractiveMoveItems );
    registerHandler<GetNets, NetsResponse>( &API_HANDLER_PCB::handleGetNets );
    registerHandler<GetNetClassForNets, NetClassForNetsResponse>(
            &API_HANDLER_PCB::handleGetNetClassForNets );
    registerHandler<RefillZones, Empty>( &API_HANDLER_PCB::handleRefillZones );

    registerHandler<SaveDocumentToString, SavedDocumentResponse>(
            &API_HANDLER_PCB::handleSaveDocumentToString );
    registerHandler<SaveSelectionToString, SavedSelectionResponse>(
            &API_HANDLER_PCB::handleSaveSelectionToString );
    registerHandler<ParseAndCreateItemsFromString, CreateItemsResponse>(
            &API_HANDLER_PCB::handleParseAndCreateItemsFromString );
    registerHandler<GetVisibleLayers, BoardLayers>( &API_HANDLER_PCB::handleGetVisibleLayers );
    registerHandler<SetVisibleLayers, Empty>( &API_HANDLER_PCB::handleSetVisibleLayers );
    registerHandler<GetActiveLayer, BoardLayerResponse>( &API_HANDLER_PCB::handleGetActiveLayer );
    registerHandler<SetActiveLayer, Empty>( &API_HANDLER_PCB::handleSetActiveLayer );
    registerHandler<GetBoardEditorAppearanceSettings, BoardEditorAppearanceSettings>(
            &API_HANDLER_PCB::handleGetBoardEditorAppearanceSettings );
    registerHandler<SetBoardEditorAppearanceSettings, Empty>(
            &API_HANDLER_PCB::handleSetBoardEditorAppearanceSettings );
    registerHandler<InjectDrcError, InjectDrcErrorResponse>(
            &API_HANDLER_PCB::handleInjectDrcError );
    registerHandler<RouteTrack, RouteTrackResponse>( &API_HANDLER_PCB::handleRouteTrack );
}


PCB_EDIT_FRAME* API_HANDLER_PCB::frame() const
{
    return static_cast<PCB_EDIT_FRAME*>( m_frame );
}


HANDLER_RESULT<RunActionResponse> API_HANDLER_PCB::handleRunAction(
        const HANDLER_CONTEXT<RunAction>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    RunActionResponse response;

    if( frame()->GetToolManager()->RunAction( aCtx.Request.action(), true ) )
        response.set_status( RunActionStatus::RAS_OK );
    else
        response.set_status( RunActionStatus::RAS_INVALID );

    return response;
}


HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_PCB::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_PCB )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse response;
    common::types::DocumentSpecifier doc;

    wxFileName fn( frame()->GetCurrentFileName() );

    doc.set_type( DocumentType::DOCTYPE_PCB );
    doc.set_board_filename( fn.GetFullName() );

    doc.mutable_project()->set_name( frame()->Prj().GetProjectName().ToStdString() );
    doc.mutable_project()->set_path( frame()->Prj().GetProjectDirectory().ToStdString() );

    response.mutable_documents()->Add( std::move( doc ) );
    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSaveDocument(
        const HANDLER_CONTEXT<SaveDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    frame()->SaveBoard();
    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSaveCopyOfDocument(
        const HANDLER_CONTEXT<SaveCopyOfDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxFileName boardPath( frame()->Prj().AbsolutePath( wxString::FromUTF8( aCtx.Request.path() ) ) );

    if( !boardPath.IsOk() || !boardPath.IsDirWritable() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' could not be opened",
                                          boardPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    if( boardPath.FileExists()
        && ( !boardPath.IsFileWritable() || !aCtx.Request.options().overwrite() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' exists and cannot be overwritten",
                                          boardPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    if( boardPath.GetExt() != FILEEXT::KiCadPcbFileExtension )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' must have a kicad_pcb extension",
                                          boardPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    BOARD* board = frame()->GetBoard();

    if( board->GetFileName().Matches( boardPath.GetFullPath() ) )
    {
        frame()->SaveBoard();
        return Empty();
    }

    bool includeProject = true;

    if( aCtx.Request.has_options() )
        includeProject = aCtx.Request.options().include_project();

    frame()->SavePcbCopy( boardPath.GetFullPath(), includeProject, /* aHeadless = */ true );

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleRevertDocument(
        const HANDLER_CONTEXT<RevertDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxFileName fn = frame()->Prj().AbsolutePath( frame()->GetBoard()->GetFileName() );

    frame()->GetScreen()->SetContentModified( false );
    frame()->ReleaseFile();
    frame()->OpenProjectFiles( std::vector<wxString>( 1, fn.GetFullPath() ), KICTL_REVERT );

    return Empty();
}


void API_HANDLER_PCB::pushCurrentCommit( const std::string& aClientName, const wxString& aMessage )
{
    API_HANDLER_EDITOR::pushCurrentCommit( aClientName, aMessage );
    frame()->Refresh();
}


std::unique_ptr<COMMIT> API_HANDLER_PCB::createCommit()
{
    return std::make_unique<BOARD_COMMIT>( frame() );
}


std::optional<BOARD_ITEM*> API_HANDLER_PCB::getItemById( const KIID& aId ) const
{
    BOARD_ITEM* item = frame()->GetBoard()->ResolveItem( aId, true );

    if( !item )
        return std::nullopt;

    return item;
}


bool API_HANDLER_PCB::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_PCB )
        return false;

    wxFileName fn( frame()->GetCurrentFileName() );
    return 0 == aDocument.board_filename().compare( fn.GetFullName() );
}


HANDLER_RESULT<std::unique_ptr<BOARD_ITEM>> API_HANDLER_PCB::createItemForType( KICAD_T aType,
        BOARD_ITEM_CONTAINER* aContainer )
{
    if( !aContainer )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tried to create an item in a null container" );
        return tl::unexpected( e );
    }

    if( aType == PCB_PAD_T && !dynamic_cast<FOOTPRINT*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a pad in {}, which is not a footprint",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }
    else if( aType == PCB_FOOTPRINT_T && !dynamic_cast<BOARD*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a footprint in {}, which is not a board",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }

    std::unique_ptr<BOARD_ITEM> created = CreateItemForType( aType, aContainer );

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


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_PCB::handleCreateUpdateItemsInternal( bool aCreate,
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

    BOARD* board = frame()->GetBoard();
    BOARD_ITEM_CONTAINER* container = board;

    if( containerResult->has_value() )
    {
        const KIID& containerId = **containerResult;
        std::optional<BOARD_ITEM*> optItem = getItemById( containerId );

        if( optItem )
        {
            container = dynamic_cast<BOARD_ITEM_CONTAINER*>( *optItem );

            if( !container )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format(
                        "The requested container {} is not a valid board item container",
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

    BOARD_COMMIT* commit = static_cast<BOARD_COMMIT*>( getCurrentCommit( aClientName ) );

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

        if( type == PCB_DIMENSION_T )
        {
            board::types::Dimension dimension;
            anyItem.UnpackTo( &dimension );

            switch( dimension.dimension_style_case() )
            {
            case board::types::Dimension::kAligned:    type = PCB_DIM_ALIGNED_T;    break;
            case board::types::Dimension::kOrthogonal: type = PCB_DIM_ORTHOGONAL_T; break;
            case board::types::Dimension::kRadial:     type = PCB_DIM_RADIAL_T;     break;
            case board::types::Dimension::kLeader:     type = PCB_DIM_LEADER_T;     break;
            case board::types::Dimension::kCenter:     type = PCB_DIM_CENTER_T;     break;
            case board::types::Dimension::DIMENSION_STYLE_NOT_SET: break;
            }
        }

        HANDLER_RESULT<std::unique_ptr<BOARD_ITEM>> creationResult =
                createItemForType( *type, container );

        if( !creationResult )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( creationResult.error().error_message() );
            aItemHandler( status, anyItem );
            continue;
        }

        std::unique_ptr<BOARD_ITEM> item( std::move( *creationResult ) );

        if( !item->Deserialize( anyItem ) )
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "could not unpack {} from request",
                                              item->GetClass().ToStdString() ) );
            return tl::unexpected( e );
        }

        std::optional<BOARD_ITEM*> optItem = getItemById( item->m_Uuid );

        if( aCreate && optItem )
        {
            status.set_code( ItemStatusCode::ISC_EXISTING );
            status.set_error_message( fmt::format( "an item with UUID {} already exists",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }
        else if( !aCreate && !optItem )
        {
            status.set_code( ItemStatusCode::ISC_NONEXISTENT );
            status.set_error_message( fmt::format( "an item with UUID {} does not exist",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        if( aCreate && !( board->GetEnabledLayers() & item->GetLayerSet() ).any() )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_DATA );
            status.set_error_message(
                "attempted to add item with no overlapping layers with the board" );
            aItemHandler( status, anyItem );
            continue;
        }

        status.set_code( ItemStatusCode::ISC_OK );
        google::protobuf::Any newItem;

        if( aCreate )
        {
            if( item->Type() == PCB_FOOTPRINT_T )
            {
                // Ensure children have unique identifiers; in case the API client created this new
                // footprint by cloning an existing one and only changing the parent UUID.
                item->RunOnChildren(
                        []( BOARD_ITEM* aChild )
                        {
                            const_cast<KIID&>( aChild->m_Uuid ) = KIID();
                        },
                        RECURSE );
            }

            item->Serialize( newItem );
            commit->Add( item.release() );
        }
        else
        {
            BOARD_ITEM* boardItem = *optItem;

            // Footprints can't be modified by CopyFrom at the moment because the commit system
            // doesn't currently know what to do with a footprint that has had its children
            // replaced with other children; which results in things like the view not having its
            // cached geometry for footprint children updated when you move a footprint around.
            // And also, groups are special because they can contain any item type, so we
            // can't use CopyFrom on them either.
            if( boardItem->Type() == PCB_FOOTPRINT_T )
            {
                // For footprints, use Modify + direct property updates instead of
                // Remove+Add.  The Remove+Add path leaves stale child items in the
                // view cache, causing ghost footprints on the canvas.
                FOOTPRINT* existingFp = static_cast<FOOTPRINT*>( boardItem );
                FOOTPRINT* newFp = static_cast<FOOTPRINT*>( item.get() );

                commit->Modify( existingFp );

                VECTOR2I newPos( newFp->GetPosition() );

                if( newPos != existingFp->GetPosition() )
                    existingFp->SetPosition( newPos );

                if( newFp->GetOrientationDegrees() != existingFp->GetOrientationDegrees() )
                    existingFp->SetOrientationDegrees( newFp->GetOrientationDegrees() );

                if( newFp->GetLayer() != existingFp->GetLayer() )
                    existingFp->Flip( existingFp->GetPosition(), FLIP_DIRECTION::TOP_BOTTOM );

                existingFp->SetLocked( newFp->IsLocked() );
                existingFp->SetReference( newFp->GetReference() );
                existingFp->SetValue( newFp->GetValue() );

                existingFp->Serialize( newItem );
            }
            else if( boardItem->Type() == PCB_GROUP_T )
            {
                PCB_GROUP* parentGroup = dynamic_cast<PCB_GROUP*>( boardItem->GetParentGroup() );

                commit->Remove( boardItem );
                item->Serialize( newItem );

                BOARD_ITEM* newBoardItem = item.release();
                commit->Add( newBoardItem );

                if( parentGroup )
                    parentGroup->AddItem( newBoardItem );
            }
            else
            {
                commit->Modify( boardItem );
                boardItem->CopyFrom( item.get() );
                boardItem->Serialize( newItem );
            }
        }

        aItemHandler( status, newItem );
    }

    if( !m_activeClients.count( aClientName ) )
    {
        pushCurrentCommit( aClientName, aCreate ? _( "Created items via API" )
                                                : _( "Modified items via API" ) );
    }


    return ItemRequestStatus::IRS_OK;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItems( const HANDLER_CONTEXT<GetItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetItemsResponse response;

    BOARD* board = frame()->GetBoard();
    std::vector<BOARD_ITEM*> items;
    std::set<KICAD_T> typesRequested, typesInserted;
    bool handledAnything = false;

    for( int typeRaw : aCtx.Request.types() )
    {
        auto typeMessage = static_cast<common::types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type == TYPE_NOT_INIT )
            continue;

        typesRequested.emplace( type );

        if( typesInserted.count( type ) )
            continue;

        switch( type )
        {
        case PCB_TRACE_T:
        case PCB_ARC_T:
        case PCB_VIA_T:
            handledAnything = true;
            std::copy( board->Tracks().begin(), board->Tracks().end(),
                       std::back_inserter( items ) );
            typesInserted.insert( { PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T } );
            break;

        case PCB_PAD_T:
        {
            handledAnything = true;

            for( FOOTPRINT* fp : board->Footprints() )
            {
                std::copy( fp->Pads().begin(), fp->Pads().end(),
                           std::back_inserter( items ) );
            }

            typesInserted.insert( PCB_PAD_T );
            break;
        }

        case PCB_FOOTPRINT_T:
        {
            handledAnything = true;

            std::copy( board->Footprints().begin(), board->Footprints().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_FOOTPRINT_T );
            break;
        }

        case PCB_SHAPE_T:
        case PCB_TEXT_T:
        case PCB_TEXTBOX_T:
        case PCB_BARCODE_T:
        {
            handledAnything = true;
            bool inserted = false;

            for( BOARD_ITEM* item : board->Drawings() )
            {
                if( item->Type() == type )
                {
                    items.emplace_back( item );
                    inserted = true;
                }
            }

            if( inserted )
                typesInserted.insert( type );

            break;
        }

        case PCB_ZONE_T:
        {
            handledAnything = true;

            std::copy( board->Zones().begin(), board->Zones().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_ZONE_T );
            break;
        }

        case PCB_GROUP_T:
        {
            handledAnything = true;

            std::copy( board->Groups().begin(), board->Groups().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_GROUP_T );
            break;
        }
        default:
            break;
        }
    }

    if( !handledAnything )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested types are valid for a Board object" );
        return tl::unexpected( e );
    }

    for( const BOARD_ITEM* item : items )
    {
        if( !typesRequested.count( item->Type() ) )
            continue;

        google::protobuf::Any itemBuf;
        item->Serialize( itemBuf );
        response.mutable_items()->Add( std::move( itemBuf ) );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItemsById(
        const HANDLER_CONTEXT<GetItemsById>& aCtx )
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

    std::vector<BOARD_ITEM*> items;

    for( const kiapi::common::types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            items.emplace_back( *item );
    }

    if( items.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested IDs were found or valid" );
        return tl::unexpected( e );
    }

    for( const BOARD_ITEM* item : items )
    {
        google::protobuf::Any itemBuf;
        item->Serialize( itemBuf );
        response.mutable_items()->Add( std::move( itemBuf ) );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}

void API_HANDLER_PCB::deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                                           const std::string& aClientName )
{
    BOARD* board = frame()->GetBoard();
    std::vector<BOARD_ITEM*> validatedItems;

    for( std::pair<const KIID, ItemDeletionStatus> pair : aItemsToDelete )
    {
        if( BOARD_ITEM* item = board->ResolveItem( pair.first, true ) )
        {
            validatedItems.push_back( item );
            aItemsToDelete[pair.first] = ItemDeletionStatus::IDS_OK;
        }

        // Note: we don't currently support locking items from API modification, but here is where
        // to add it in the future (and return IDS_IMMUTABLE)
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    for( BOARD_ITEM* item : validatedItems )
        commit->Remove( item );

    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, _( "Deleted items via API" ) );
}


std::optional<EDA_ITEM*> API_HANDLER_PCB::getItemFromDocument( const DocumentSpecifier& aDocument,
                                                               const KIID& aId )
{
    if( !validateDocument( aDocument ) )
        return std::nullopt;

    return getItemById( aId );
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleGetSelection(
            const HANDLER_CONTEXT<GetSelection>& aCtx )
{
    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    std::set<KICAD_T> filter;

    for( int typeRaw : aCtx.Request.types() )
    {
        auto typeMessage = static_cast<types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type == TYPE_NOT_INIT )
            continue;

        filter.insert( type );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
    {
        if( filter.empty() || filter.contains( item->Type() ) )
            item->Serialize( *response.add_items() );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleClearSelection(
        const HANDLER_CONTEXT<ClearSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    mgr->RunAction( ACTIONS::selectionClear );
    frame()->Refresh();

    return Empty();
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleAddToSelection(
        const HANDLER_CONTEXT<AddToSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    std::vector<EDA_ITEM*> toAdd;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toAdd.emplace_back( *item );
    }

    selectionTool->AddItemsToSel( &toAdd );
    frame()->Refresh();

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleRemoveFromSelection(
        const HANDLER_CONTEXT<RemoveFromSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    std::vector<EDA_ITEM*> toRemove;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toRemove.emplace_back( *item );
    }

    selectionTool->RemoveItemsFromSel( &toRemove );
    frame()->Refresh();

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<BoardStackupResponse> API_HANDLER_PCB::handleGetStackup(
        const HANDLER_CONTEXT<GetBoardStackup>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardStackupResponse  response;
    google::protobuf::Any any;

    frame()->GetBoard()->GetStackupOrDefault().Serialize( any );

    any.UnpackTo( response.mutable_stackup() );

    // User-settable layer names are not stored in BOARD_STACKUP at the moment
    for( board::BoardStackupLayer& layer : *response.mutable_stackup()->mutable_layers() )
    {
        if( layer.type() == board::BoardStackupLayerType::BSLT_DIELECTRIC )
            continue;

        PCB_LAYER_ID id = FromProtoEnum<PCB_LAYER_ID>( layer.layer() );
        wxCHECK2( id != UNDEFINED_LAYER, continue );

        layer.set_user_name( frame()->GetBoard()->GetLayerName( id ) );
    }

    return response;
}


HANDLER_RESULT<BoardEnabledLayersResponse> API_HANDLER_PCB::handleGetBoardEnabledLayers(
        const HANDLER_CONTEXT<GetBoardEnabledLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardEnabledLayersResponse response;

    BOARD* board = frame()->GetBoard();
    int copperLayerCount = board->GetCopperLayerCount();

    response.set_copper_layer_count( copperLayerCount );

    LSET enabled = board->GetEnabledLayers();

    // The Rescue layer is an internal detail and should be hidden from the API
    enabled.reset( Rescue );

    // Just in case this is out of sync; the API should always return the expected copper layers
    enabled |= LSET::AllCuMask( copperLayerCount );

    board::PackLayerSet( *response.mutable_layers(), enabled );

    return response;
}


HANDLER_RESULT<BoardEnabledLayersResponse> API_HANDLER_PCB::handleSetBoardEnabledLayers(
        const HANDLER_CONTEXT<SetBoardEnabledLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( aCtx.Request.copper_layer_count() % 2 != 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "copper_layer_count must be an even number" );
        return tl::unexpected( e );
    }

    if( aCtx.Request.copper_layer_count() > MAX_CU_LAYERS )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "copper_layer_count must be below %d", MAX_CU_LAYERS ) );
        return tl::unexpected( e );
    }

    int copperLayerCount = static_cast<int>( aCtx.Request.copper_layer_count() );
    LSET enabled = board::UnpackLayerSet( aCtx.Request.layers() );

    // Sanitize the input
    enabled |= LSET( { Edge_Cuts, Margin, F_CrtYd, B_CrtYd } );
    enabled &= ~LSET::AllCuMask();
    enabled |= LSET::AllCuMask( copperLayerCount );

    BOARD* board = frame()->GetBoard();

    LSET previousEnabled = board->GetEnabledLayers();
    LSET changedLayers = enabled ^ previousEnabled;

    board->SetEnabledLayers( enabled );
    board->SetVisibleLayers( board->GetVisibleLayers() | changedLayers );

    LSEQ removedLayers;

    for( PCB_LAYER_ID layer_id : previousEnabled )
    {
        if( !enabled[layer_id] && board->HasItemsOnLayer( layer_id ) )
            removedLayers.push_back( layer_id );
    }

    bool modified = false;

    if( !removedLayers.empty() )
    {
        m_frame->GetToolManager()->RunAction( PCB_ACTIONS::selectionClear );

        for( PCB_LAYER_ID layer_id : removedLayers )
            modified |= board->RemoveAllItemsOnLayer( layer_id );
    }

    if( enabled != previousEnabled )
        frame()->UpdateUserInterface();

    if( modified )
        frame()->OnModify();

    BoardEnabledLayersResponse response;

    response.set_copper_layer_count( copperLayerCount );
    board::PackLayerSet( *response.mutable_layers(), enabled );

    return response;
}


HANDLER_RESULT<GraphicsDefaultsResponse> API_HANDLER_PCB::handleGetGraphicsDefaults(
        const HANDLER_CONTEXT<GetGraphicsDefaults>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    const BOARD_DESIGN_SETTINGS& bds = frame()->GetBoard()->GetDesignSettings();
    GraphicsDefaultsResponse response;

    // TODO: This should change to be an enum class
    constexpr std::array<kiapi::board::BoardLayerClass, LAYER_CLASS_COUNT> classOrder = {
        kiapi::board::BLC_SILKSCREEN,
        kiapi::board::BLC_COPPER,
        kiapi::board::BLC_EDGES,
        kiapi::board::BLC_COURTYARD,
        kiapi::board::BLC_FABRICATION,
        kiapi::board::BLC_OTHER
    };

    for( int i = 0; i < LAYER_CLASS_COUNT; ++i )
    {
        kiapi::board::BoardLayerGraphicsDefaults* l = response.mutable_defaults()->add_layers();

        l->set_layer( classOrder[i] );
        l->mutable_line_thickness()->set_value_nm( bds.m_LineThickness[i] );

        kiapi::common::types::TextAttributes* text = l->mutable_text();
        text->mutable_size()->set_x_nm( bds.m_TextSize[i].x );
        text->mutable_size()->set_y_nm( bds.m_TextSize[i].y );
        text->mutable_stroke_width()->set_value_nm( bds.m_TextThickness[i] );
        text->set_italic( bds.m_TextItalic[i] );
        text->set_keep_upright( bds.m_TextUpright[i] );
    }

    return response;
}


HANDLER_RESULT<types::Vector2> API_HANDLER_PCB::handleGetBoardOrigin(
        const HANDLER_CONTEXT<GetBoardOrigin>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    VECTOR2I origin;
    const BOARD_DESIGN_SETTINGS& settings = frame()->GetBoard()->GetDesignSettings();

    switch( aCtx.Request.type() )
    {
    case BOT_GRID:
        origin = settings.GetGridOrigin();
        break;

    case BOT_DRILL:
        origin = settings.GetAuxOrigin();
        break;

    default:
    case BOT_UNKNOWN:
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Unexpected origin type" );
        return tl::unexpected( e );
    }
    }

    types::Vector2 reply;
    PackVector2( reply, origin );
    return reply;
}

HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetBoardOrigin(
        const HANDLER_CONTEXT<SetBoardOrigin>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    VECTOR2I origin = UnpackVector2( aCtx.Request.origin() );

    switch( aCtx.Request.type() )
    {
    case BOT_GRID:
    {
        PCB_EDIT_FRAME* f = frame();

        frame()->CallAfter( [f, origin]()
                            {
                                // gridSetOrigin takes ownership and frees this
                                VECTOR2D* dorigin = new VECTOR2D( origin );
                                TOOL_MANAGER* mgr = f->GetToolManager();
                                mgr->RunAction( PCB_ACTIONS::gridSetOrigin, dorigin );
                                f->Refresh();
                            } );
        break;
    }

    case BOT_DRILL:
    {
        PCB_EDIT_FRAME* f = frame();

        frame()->CallAfter( [f, origin]()
                            {
                                TOOL_MANAGER* mgr = f->GetToolManager();
                                mgr->RunAction( PCB_ACTIONS::drillSetOrigin, origin );
                                f->Refresh();
                            } );
        break;
    }

    default:
    case BOT_UNKNOWN:
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Unexpected origin type" );
        return tl::unexpected( e );
    }
    }

    return Empty();
}


HANDLER_RESULT<BoardLayerNameResponse> API_HANDLER_PCB::handleGetBoardLayerName(
            const HANDLER_CONTEXT<GetBoardLayerName>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    BoardLayerNameResponse response;

    PCB_LAYER_ID id = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );

    response.set_name( frame()->GetBoard()->GetLayerName( id ) );

    return response;
}


HANDLER_RESULT<GetBoundingBoxResponse> API_HANDLER_PCB::handleGetBoundingBox(
        const HANDLER_CONTEXT<GetBoundingBox>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetBoundingBoxResponse response;
    bool includeText = aCtx.Request.mode() == BoundingBoxMode::BBM_ITEM_AND_CHILD_TEXT;

    for( const types::KIID& idMsg : aCtx.Request.items() )
    {
        KIID id( idMsg.value() );
        std::optional<BOARD_ITEM*> optItem = getItemById( id );

        if( !optItem )
            continue;

        BOARD_ITEM* item = *optItem;
        BOX2I bbox;

        if( item->Type() == PCB_FOOTPRINT_T )
            bbox = static_cast<FOOTPRINT*>( item )->GetBoundingBox( includeText );
        else
            bbox = item->GetBoundingBox();

        response.add_items()->set_value( idMsg.value() );
        PackBox2( *response.add_boxes(), bbox );
    }

    return response;
}


HANDLER_RESULT<PadShapeAsPolygonResponse> API_HANDLER_PCB::handleGetPadShapeAsPolygon(
        const HANDLER_CONTEXT<GetPadShapeAsPolygon>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    PadShapeAsPolygonResponse response;
    PCB_LAYER_ID layer = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aCtx.Request.layer() );

    for( const types::KIID& padRequest : aCtx.Request.pads() )
    {
        KIID id( padRequest.value() );
        std::optional<BOARD_ITEM*> optPad = getItemById( id );

        if( !optPad || ( *optPad )->Type() != PCB_PAD_T )
            continue;

        response.add_pads()->set_value( padRequest.value() );

        PAD* pad = static_cast<PAD*>( *optPad );
        SHAPE_POLY_SET poly;
        pad->TransformShapeToPolygon( poly, pad->Padstack().EffectiveLayerFor( layer ), 0,
                                      pad->GetMaxError(), ERROR_INSIDE );

        types::PolygonWithHoles* polyMsg = response.mutable_polygons()->Add();
        PackPolyLine( *polyMsg->mutable_outline(), poly.COutline( 0 ) );
    }

    return response;
}


HANDLER_RESULT<PadstackPresenceResponse> API_HANDLER_PCB::handleCheckPadstackPresenceOnLayers(
        const HANDLER_CONTEXT<CheckPadstackPresenceOnLayers>& aCtx )
{
    using board::types::BoardLayer;

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    PadstackPresenceResponse response;

    LSET layers;

    for( const int layer : aCtx.Request.layers() )
        layers.set( FromProtoEnum<PCB_LAYER_ID, BoardLayer>( static_cast<BoardLayer>( layer ) ) );

    for( const types::KIID& padRequest : aCtx.Request.items() )
    {
        KIID id( padRequest.value() );
        std::optional<BOARD_ITEM*> optItem = getItemById( id );

        if( !optItem )
            continue;

        switch( ( *optItem )->Type() )
        {
        case PCB_PAD_T:
        {
            PAD* pad = static_cast<PAD*>( *optItem );

            for( PCB_LAYER_ID layer : layers )
            {
                PadstackPresenceEntry* entry = response.add_entries();
                entry->mutable_item()->set_value( pad->m_Uuid.AsStdString() );
                entry->set_layer( ToProtoEnum<PCB_LAYER_ID, BoardLayer>( layer ) );
                entry->set_presence( pad->FlashLayer( layer ) ? PSP_PRESENT : PSP_NOT_PRESENT );
            }

            break;
        }

        case PCB_VIA_T:
        {
            PCB_VIA* via = static_cast<PCB_VIA*>( *optItem );

            for( PCB_LAYER_ID layer : layers )
            {
                PadstackPresenceEntry* entry = response.add_entries();
                entry->mutable_item()->set_value( via->m_Uuid.AsStdString() );
                entry->set_layer( ToProtoEnum<PCB_LAYER_ID, BoardLayer>( layer ) );
                entry->set_presence( via->FlashLayer( layer ) ? PSP_PRESENT : PSP_NOT_PRESENT );
            }

            break;
        }

        default:
            break;
        }
    }

    return response;
}


HANDLER_RESULT<types::TitleBlockInfo> API_HANDLER_PCB::handleGetTitleBlockInfo(
        const HANDLER_CONTEXT<GetTitleBlockInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    const TITLE_BLOCK& block = board->GetTitleBlock();

    types::TitleBlockInfo response;

    response.set_title( block.GetTitle().ToUTF8() );
    response.set_date( block.GetDate().ToUTF8() );
    response.set_revision( block.GetRevision().ToUTF8() );
    response.set_company( block.GetCompany().ToUTF8() );
    response.set_comment1( block.GetComment( 0 ).ToUTF8() );
    response.set_comment2( block.GetComment( 1 ).ToUTF8() );
    response.set_comment3( block.GetComment( 2 ).ToUTF8() );
    response.set_comment4( block.GetComment( 3 ).ToUTF8() );
    response.set_comment5( block.GetComment( 4 ).ToUTF8() );
    response.set_comment6( block.GetComment( 5 ).ToUTF8() );
    response.set_comment7( block.GetComment( 6 ).ToUTF8() );
    response.set_comment8( block.GetComment( 7 ).ToUTF8() );
    response.set_comment9( block.GetComment( 8 ).ToUTF8() );

    return response;
}


HANDLER_RESULT<ExpandTextVariablesResponse> API_HANDLER_PCB::handleExpandTextVariables(
    const HANDLER_CONTEXT<ExpandTextVariables>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    ExpandTextVariablesResponse reply;
    BOARD* board = frame()->GetBoard();

    std::function<bool( wxString* )> textResolver =
            [&]( wxString* token ) -> bool
            {
                // Handles m_board->GetTitleBlock() *and* m_board->GetProject()
                return board->ResolveTextVar( token, 0 );
            };

    for( const std::string& textMsg : aCtx.Request.text() )
    {
        wxString text = ExpandTextVars( wxString::FromUTF8( textMsg ), &textResolver );
        reply.add_text( text.ToUTF8() );
    }

    return reply;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleInteractiveMoveItems(
        const HANDLER_CONTEXT<InteractiveMoveItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    std::vector<EDA_ITEM*> toSelect;

    for( const kiapi::common::types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toSelect.emplace_back( static_cast<EDA_ITEM*>( *item ) );
    }

    if( toSelect.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "None of the given items exist on the board",
                                          aCtx.Request.board().board_filename() ) );
        return tl::unexpected( e );
    }

    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();
    selectionTool->GetSelection().SetReferencePoint( toSelect[0]->GetPosition() );

    mgr->RunAction( ACTIONS::selectionClear );
    mgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &toSelect );

    COMMIT* commit = getCurrentCommit( aCtx.ClientName );
    mgr->PostAPIAction( PCB_ACTIONS::move, commit );

    return Empty();
}


HANDLER_RESULT<NetsResponse> API_HANDLER_PCB::handleGetNets( const HANDLER_CONTEXT<GetNets>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    NetsResponse response;
    BOARD* board = frame()->GetBoard();

    std::set<wxString> netclassFilter;

    for( const std::string& nc : aCtx.Request.netclass_filter() )
        netclassFilter.insert( wxString( nc.c_str(), wxConvUTF8 ) );

    for( NETINFO_ITEM* net : board->GetNetInfo() )
    {
        NETCLASS* nc = net->GetNetClass();

        if( !netclassFilter.empty() && nc )
        {
            bool inClass = false;

            for( const wxString& filter : netclassFilter )
            {
                if( nc->ContainsNetclassWithName( filter ) )
                {
                    inClass = true;
                    break;
                }
            }

            if( !inClass )
                continue;
        }

        board::types::Net* netProto = response.add_nets();
        netProto->set_name( net->GetNetname() );
        netProto->mutable_code()->set_value( net->GetNetCode() );
    }

    return response;
}


HANDLER_RESULT<NetClassForNetsResponse> API_HANDLER_PCB::handleGetNetClassForNets(
            const HANDLER_CONTEXT<GetNetClassForNets>& aCtx )
{
    NetClassForNetsResponse response;

    BOARD* board = frame()->GetBoard();
    const NETINFO_LIST& nets = board->GetNetInfo();
    google::protobuf::Any any;

    for( const board::types::Net& net : aCtx.Request.net() )
    {
        NETINFO_ITEM* netInfo = nets.GetNetItem( wxString::FromUTF8( net.name() ) );

        if( !netInfo )
            continue;

        netInfo->GetNetClass()->Serialize( any );
        auto [pair, rc] = response.mutable_classes()->insert( { net.name(), {} } );
        any.UnpackTo( &pair->second );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleRefillZones( const HANDLER_CONTEXT<RefillZones>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( aCtx.Request.zones().empty() )
    {
        TOOL_MANAGER* mgr = frame()->GetToolManager();
        frame()->CallAfter( [mgr]()
                            {
                                mgr->RunAction( PCB_ACTIONS::zoneFillAll );
                            } );
    }
    else
    {
        // TODO
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
        return tl::unexpected( e );
    }

    return Empty();
}


HANDLER_RESULT<RouteTrackResponse> API_HANDLER_PCB::handleRouteTrack(
        const HANDLER_CONTEXT<RouteTrack>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    RouteTrackResponse response;

    // Structured failure feedback: the message is a compact JSON object so an agentic caller can
    // react (reroute the blocker, allow a via, change layer) instead of blindly retrying.
    auto fail = [&]( const std::string& aReason, const std::string& aDetail ) -> RouteTrackResponse
    {
        response.set_success( false );
        response.set_message( "{\"reason\":\"" + aReason + "\",\"detail\":\"" + aDetail + "\"}" );
        return response;
    };

    BOARD*        board = frame()->GetBoard();
    TOOL_MANAGER* mgr = frame()->GetToolManager();
    ROUTER_TOOL*  routerTool = mgr ? mgr->GetTool<ROUTER_TOOL>() : nullptr;

    if( !routerTool )
        return fail( "error", "Router tool not available" );

    // Build a fresh, self-contained PNS router bound to the live board + view, rather than
    // mutating the interactive ROUTER_TOOL's shared router (unsafe to ClearWorld here).
    // Mirrors generator_tool_pns_proxy.cpp Reset().
    PNS_KICAD_IFACE* iface = new PNS_KICAD_IFACE;
    iface->SetBoard( board );
    iface->SetView( frame()->GetCanvas()->GetView() );
    iface->SetHostTool( routerTool );

    PNS::ROUTER* router = new PNS::ROUTER;
    router->SetInterface( iface );
    router->ClearWorld();
    router->SyncWorld();

    PCBNEW_SETTINGS* appSettings = frame()->GetPcbNewSettings();

    if( appSettings )
    {
        if( !appSettings->m_PnsSettings )
            appSettings->m_PnsSettings =
                    std::make_unique<PNS::ROUTING_SETTINGS>( appSettings, "tools.pns" );

        router->LoadSettings( appSettings->m_PnsSettings.get() );
    }

    const VECTOR2I startPt( aCtx.Request.start().x_nm(), aCtx.Request.start().y_nm() );
    const VECTOR2I endPt( aCtx.Request.end().x_nm(), aCtx.Request.end().y_nm() );

    PCB_LAYER_ID pcbLayer = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );
    int          pnsLayer = iface->GetPNSLayerFromBoardLayer( pcbLayer );
    int          slop = 75000; // 0.075 mm hit radius to reliably catch a pad/track anchor

    // Resolve a routable anchor (pad/track/via on the routing layer) at a point.
    auto pickItem = [&]( const VECTOR2I& aPt ) -> PNS::ITEM*
    {
        PNS::ITEM_SET candidates = router->QueryHoverItems( aPt, slop );
        PNS::ITEM*    best = nullptr;

        for( PNS::ITEM* item : candidates.Items() )
        {
            if( !item->OfKind( PNS::ITEM::SOLID_T | PNS::ITEM::SEGMENT_T | PNS::ITEM::ARC_T
                               | PNS::ITEM::VIA_T ) )
                continue;

            if( !item->Layers().Overlaps( pnsLayer ) )
                continue;

            if( item->OfKind( PNS::ITEM::SOLID_T ) )   // prefer pads as anchors
                return item;

            if( !best )
                best = item;
        }

        return best;
    };

    PNS::ITEM* startItem = pickItem( startPt );
    PNS::ITEM* endItem = pickItem( endPt );

    if( !startItem )
        return fail( "no_start_anchor",
                     "No routable copper (pad/track) at the start point on the given layer" );

    // Vias are OPT-IN (last resort): auto-via only when the caller supplied a via diameter. With
    // no via budget the route stays single-layer and, if boxed in, fails and reports the blocker so
    // the agent can reroute the blocker rather than lean on vias.
    const bool allowVia = aCtx.Request.via_diameter_nm() > 0 && !aCtx.Request.diff_pair();

    // Diagnostics surfaced in the structured response message.
    std::string g_reason = "committed";
    std::string g_blockNet, g_blockRef;
    bool        g_viaUsed = false;
    bool        g_viaWouldHelp = false;
    VECTOR2I    g_stall( 0, 0 );

    // Identify the nearest other-net copper between a stalled head and the target -- the "blocker"
    // shove could not get past. Walk several probe points along the path so we catch an obstacle
    // that is a little ahead of where the head actually stopped.
    auto findBlocker = [&]( const VECTOR2I& aStall, const VECTOR2I& aTarget )
    {
        VECTOR2I     dir = aTarget - aStall;
        const double len = dir.EuclideanNorm();

        // Walk the whole corridor from the stall toward the target at ~0.2 mm resolution, nearest
        // first, so we name the first other-net copper standing in the way.
        for( int step = 0; step <= 80; ++step )
        {
            const double dist = step * 200000.0;

            if( dist > len )
                break;

            VECTOR2I probe = ( len < 1 ) ? aStall : aStall + dir.Resize( static_cast<int>( dist ) );

            PNS::ITEM_SET hits = router->QueryHoverItems( probe, 300000 );

            for( PNS::ITEM* it : hits.Items() )
            {
                if( !it->OfKind( PNS::ITEM::SOLID_T | PNS::ITEM::SEGMENT_T | PNS::ITEM::ARC_T
                                 | PNS::ITEM::VIA_T ) )
                    continue;

                if( !it->Layers().Overlaps( router->GetCurrentLayer() ) )
                    continue;

                if( startItem && it->Net() == startItem->Net() )
                    continue;

                BOARD_ITEM* p = it->Parent();

                if( !p )
                    continue;

                if( p->IsConnected() )
                    g_blockNet = static_cast<BOARD_CONNECTED_ITEM*>( p )->GetNetname().ToStdString();

                if( FOOTPRINT* fp = p->GetParentFootprint() )
                    g_blockRef = fp->GetReference().ToStdString();

                return;
            }
        }
    };

    switch( aCtx.Request.mode() )
    {
    case RM_WALK_AROUND: router->Settings().SetMode( PNS::RM_Walkaround );    break;
    case RM_NO_SHOVE:    router->Settings().SetMode( PNS::RM_MarkObstacles ); break;
    default:             router->Settings().SetMode( PNS::RM_Shove );         break;
    }

    router->SetMode( aCtx.Request.diff_pair() ? PNS::PNS_MODE_ROUTE_DIFF_PAIR
                                              : PNS::PNS_MODE_ROUTE_SINGLE );

    PNS::SIZES_SETTINGS sizes = router->Sizes();
    iface->ImportSizes( sizes, startItem, startItem ? startItem->Net() : nullptr, startPt );

    if( aCtx.Request.width_nm() > 0 )
    {
        sizes.SetTrackWidth( static_cast<int>( aCtx.Request.width_nm() ) );
        sizes.SetTrackWidthIsExplicit( true );
    }

    if( aCtx.Request.via_diameter_nm() > 0 )
        sizes.SetViaDiameter( static_cast<int>( aCtx.Request.via_diameter_nm() ) );

    if( aCtx.Request.via_drill_nm() > 0 )
        sizes.SetViaDrill( static_cast<int>( aCtx.Request.via_drill_nm() ) );

    router->UpdateSizes( sizes );

    if( !router->StartRouting( startPt, startItem, pnsLayer ) )
        return fail( "start_failed", "StartRouting failed (no net/anchor at the start point?)" );

    for( const kiapi::common::types::Vector2& wp : aCtx.Request.waypoints() )
        router->Move( VECTOR2I( wp.x_nm(), wp.y_nm() ), nullptr );

    auto d2 = []( const std::string& m )
    {
        if( FILE* f = fopen( "C:\\Users\\Robert\\Programs\\eda-expert\\temp\\route_dbg2.txt", "a" ) )
        { fputs( m.c_str(), f ); fputs( "\n", f ); fclose( f ); }
    };

    router->Move( endPt, endItem );

    const int reachTol = 200000; // 0.2 mm: head is "at" the target (allows pad-anchor snap)

    auto curEnd = [&]() -> VECTOR2I
    {
        return router->Placer() ? router->Placer()->CurrentEnd() : startPt;
    };

    auto reached = [&]( const VECTOR2I& aTarget ) -> bool
    {
        return ( curEnd() - aTarget ).EuclideanNorm() <= reachTol;
    };

    bool reachedEnd = reached( endPt );
    d2( "ENTER endPt=" + std::to_string( endPt.x ) + "," + std::to_string( endPt.y )
        + " curEnd=" + std::to_string( curEnd().x ) + "," + std::to_string( curEnd().y )
        + " reached1=" + std::to_string( reachedEnd ) );

    // Auto-via: when a single-layer route can't reach the endpoint (boxed out by other-net
    // copper that shove can't clear), drop a via at the furthest reachable point and continue
    // on the paired (opposite) copper layer, which is typically clear.
    if( !reachedEnd && allowVia )
    {
        VECTOR2I     stall = curEnd();
        PCB_LAYER_ID curBoardLayer = iface->GetBoardLayerFromPNSLayer( router->GetCurrentLayer() );
        PCB_LAYER_ID tgtBoardLayer = ( curBoardLayer == F_Cu ) ? B_Cu : F_Cu;
        int          curPNS = iface->GetPNSLayerFromBoardLayer( curBoardLayer );
        int          tgtPNS = iface->GetPNSLayerFromBoardLayer( tgtBoardLayer );

        d2( "  viablock stall=" + std::to_string( stall.x ) + "," + std::to_string( stall.y )
            + " curBoardLayer=" + std::to_string( curBoardLayer )
            + " tgtBoardLayer=" + std::to_string( tgtBoardLayer )
            + " progress=" + std::to_string( ( stall - startPt ).EuclideanNorm() ) );

        if( router->RoutingInProgress() && ( stall - startPt ).EuclideanNorm() > reachTol )
        {
            // Mirror ROUTER_TOOL::handleLayerSwitch(aForceVia) + switchLayerOnViaPlacement():
            // register the THROUGH-via layer pair so a valid via can actually be formed, arm
            // via placement, fix the via at the stall point (FixRoute returning false means
            // "route continues"), switch to the paired layer, then keep moving to the endpoint.
            PNS::SIZES_SETTINGS sizes2 = router->Sizes();
            sizes2.ClearLayerPairs();
            sizes2.SetViaType( VIATYPE::THROUGH );
            sizes2.AddLayerPair( curPNS, tgtPNS );
            router->UpdateSizes( sizes2 );

            if( !router->IsPlacingVia() )
                router->ToggleViaPlacement();

            router->Move( stall, nullptr );   // attach the armed via to the head at the stall
            bool done = router->FixRoute( stall, nullptr, /*forceFinish*/ false,
                                          /*forceCommit*/ false );

            std::optional<int> newLayer = router->Sizes().PairedLayer( router->GetCurrentLayer() );

            if( !newLayer )
                newLayer = tgtPNS;

            bool switched = router->SwitchLayer( *newLayer );

            d2( "  done=" + std::to_string( done ) + " switched=" + std::to_string( switched )
                + " newLayer=" + std::to_string( *newLayer )
                + " curLayer=" + std::to_string( router->GetCurrentLayer() ) );

            if( !done )
                router->Move( endPt, endItem );

            reachedEnd = reached( endPt );
            d2( "  after2 curEnd=" + std::to_string( curEnd().x ) + ","
                + std::to_string( curEnd().y ) + " reached2=" + std::to_string( reachedEnd ) );
        }
    }

    // If we ended on a layer the target pad doesn't reach, enable a via so FixRoute lands on it.
    if( endItem && !endItem->Layers().Overlaps( router->GetCurrentLayer() )
        && !router->IsPlacingVia() )
    {
        router->ToggleViaPlacement();
    }

    // Blocked: the head could not reach the endpoint on this layer (and either vias were not
    // permitted, or even a via could not clear it). Record where it stalled and what is in the way
    // so the caller can reroute the blocker (or opt to allow a via / change layer).
    if( !reachedEnd )
    {
        g_reason = "blocked";
        g_stall  = curEnd();

        findBlocker( g_stall, endPt );

        // A single-layer attempt that is boxed in can usually escape with a via; flag it so the
        // caller can choose between rerouting the blocker (preferred) and allowing a via.
        g_viaWouldHelp = !allowVia;
    }

    // Verify the (possibly shoved) placement is collision-free BEFORE committing. PNS shove-mode
    // FixRoute only rejects collisions against pads (a documented workaround in line_placer), so a
    // headless commit can otherwise push a route that still violates track/via clearance. We check
    // the full live trace against the placement node and refuse to commit anything dirty.
    bool clean = reachedEnd;

    if( clean )
    {
        PNS::PLACEMENT_ALGO* placer = router->Placer();
        PNS::NODE*           node = placer ? placer->CurrentNode( true ) : nullptr;

        if( !placer || !node || node->CheckColliding( placer->Traces() ) )
            clean = false;
    }

    // Snapshot existing copper so we can report what the route created (PCB_VIA derives PCB_TRACK).
    std::set<KIID> before;

    for( PCB_TRACK* t : board->Tracks() )
        before.insert( t->m_Uuid );

    BOARD_DESIGN_SETTINGS&       bds = board->GetDesignSettings();
    std::shared_ptr<DRC_ENGINE>& drc = bds.m_DRCEngine;

    // True if two copper items of DIFFERENT nets are closer than their required clearance on any
    // shared copper layer.
    auto collides = [&]( BOARD_CONNECTED_ITEM* a, BOARD_CONNECTED_ITEM* b ) -> bool
    {
        if( !a || !b || a->GetNetCode() == b->GetNetCode() )
            return false;

        LSET common = a->GetLayerSet() & b->GetLayerSet() & LSET::AllCuMask();

        for( PCB_LAYER_ID lyr : common.Seq() )
        {
            int minClear = bds.m_MinClearance;

            if( drc )
                minClear = drc->EvalRules( CLEARANCE_CONSTRAINT, a, b, lyr ).GetValue().Min();

            std::shared_ptr<SHAPE> sa = a->GetEffectiveShape( lyr );
            std::shared_ptr<SHAPE> sb = b->GetEffectiveShape( lyr );

            if( sa && sb && sa->Collide( sb.get(), minClear ) )
                return true;
        }

        return false;
    };

    // Count clearance violations between one net's tracks and all other-net copper. Comparing this
    // before vs after the route catches not only the route's own new copper but also any pre-existing
    // same-net segment whose geometry a post-route track cleanup/merge may have nudged into a
    // violation.
    auto countNetClearance = [&]( int net ) -> int
    {
        if( net < 0 )
            return 0;

        int count = 0;

        for( PCB_TRACK* a : board->Tracks() )
        {
            if( a->GetNetCode() != net )
                continue;

            for( PCB_TRACK* b : board->Tracks() )
                if( collides( a, b ) )
                    count++;

            for( FOOTPRINT* fp : board->Footprints() )
                for( PAD* pad : fp->Pads() )
                    if( collides( a, pad ) )
                        count++;
        }

        return count;
    };

    const int routedNet = ( startItem && startItem->Parent() && startItem->Parent()->IsConnected() )
            ? static_cast<BOARD_CONNECTED_ITEM*>( startItem->Parent() )->GetNetCode()
            : -1;

    // Capture connectivity + the routed net's clearance health to verify the commit afterwards.
    const unsigned beforeUnconn   = board->GetConnectivity()->GetUnconnectedCount( false );
    const int      beforeNetClear = countNetClearance( routedNet );

    // Snap the final landing point onto the end item's nearest anchor (within tolerance) so the
    // route joins the target exactly instead of stopping a few microns short, which would dangle
    // (and be rejected by the connectivity check below).
    VECTOR2I targetPt = endPt;

    if( endItem )
    {
        bool snapped = false;

        for( int i = 0; i < endItem->AnchorCount(); ++i )
        {
            if( ( endItem->Anchor( i ) - endPt ).EuclideanNorm() <= reachTol )
            {
                targetPt = endItem->Anchor( i );
                snapped  = true;
                break;
            }
        }

        // No nearby anchor (pad/track-end): if the target is a track, snap to the exact nearest
        // point ON the track so a mid-span T-join actually connects instead of landing microns off.
        if( !snapped && endItem->OfKind( PNS::ITEM::SEGMENT_T ) )
            targetPt = static_cast<PNS::SEGMENT*>( endItem )->Seg().NearestPoint( endPt );
    }

    bool ok = false;

    if( clean )
        ok = router->FixRoute( targetPt, endItem, /*forceFinish*/ true, /*forceCommit*/ false );

    d2( "  clean=" + std::to_string( clean ) + " FixRoute ok=" + std::to_string( ok )
        + " reachedEnd=" + std::to_string( reachedEnd )
        + " beforeTracks=" + std::to_string( before.size() ) );

    // Only commit a clean, complete route -- never leave a partial stub or a clearance violation.
    if( ok )
        router->CommitRouting();   // CommitPlacement() pushes the BOARD_COMMIT, then stops
    else
        router->StopRouting();

    d2( "  afterCommit boardTracks=" + std::to_string( board->Tracks().size() ) );

    delete router;   // delete router before iface (NODE dtor needs the iface's rule resolver)
    delete iface;

    bool committed = ok && reachedEnd;

    // Post-commit verification against the REAL engines (PNS's own model misses hole/zone clearance
    // and cannot see connectivity). Either failure reverts the whole commit (including any shoved
    // neighbours) via undo so the board is never left dirty/dangling:
    //   1. Connectivity -- the route must actually reduce the unconnected count. PNS can land a
    //      route just short of the target, leaving a dangling stub that does not join the net.
    //   2. Clearance    -- the routed net must not GAIN any clearance violation against other-net
    //      copper (covers the route's own copper and any cleanup-shifted same-net segment).
    if( committed )
    {
        board->GetConnectivity()->RecalculateRatsnest();
        const unsigned afterUnconn   = board->GetConnectivity()->GetUnconnectedCount( false );
        const int      afterNetClear = countNetClearance( routedNet );
        const bool     connectedGain = afterUnconn < beforeUnconn;
        const bool     clearanceOk   = afterNetClear <= beforeNetClear;

        if( !connectedGain || !clearanceOk )
        {
            frame()->GetToolManager()->RunAction( ACTIONS::undo );   // synchronous (aNow=true)
            board->GetConnectivity()->RecalculateRatsnest();
            committed = false;
            g_reason  = !connectedGain ? "connectivity_fail" : "clearance_fail";
            d2( "  REVERTED: connectedGain=" + std::to_string( connectedGain )
                + " clearanceOk=" + std::to_string( clearanceOk )
                + " unconn " + std::to_string( beforeUnconn ) + "->" + std::to_string( afterUnconn )
                + " netClear " + std::to_string( beforeNetClear ) + "->"
                + std::to_string( afterNetClear ) );
        }
    }

    if( committed )
    {
        for( PCB_TRACK* t : board->Tracks() )
        {
            if( !before.count( t->m_Uuid ) )
            {
                response.add_created_items()->set_value( t->m_Uuid.AsString().ToStdString() );

                if( t->Type() == PCB_VIA_T )
                    g_viaUsed = true;
            }
        }
    }

    if( frame()->GetCanvas() )
        frame()->GetCanvas()->Refresh();

    response.set_success( committed );

    // Compact JSON so an agent can decide what to do next (reroute the blocker, allow a via, change
    // layer) instead of blindly retrying. reason in {committed, blocked, connectivity_fail,
    // clearance_fail}; on 'blocked' we also report where it stalled and what was in the way.
    std::string msg = "{\"reason\":\"" + g_reason
                      + "\",\"via_used\":" + ( g_viaUsed ? "true" : "false" );

    if( g_reason == "blocked" )
    {
        char buf[80];
        snprintf( buf, sizeof( buf ), ",\"stall_mm\":[%.3f,%.3f]", g_stall.x / 1e6, g_stall.y / 1e6 );
        msg += buf;
        msg += ",\"layer\":\"" + board->GetLayerName( pcbLayer ).ToStdString() + "\"";

        if( !g_blockNet.empty() || !g_blockRef.empty() )
            msg += ",\"blocker\":{\"net\":\"" + g_blockNet + "\",\"ref\":\"" + g_blockRef + "\"}";

        msg += ",\"via_would_help\":" + std::string( g_viaWouldHelp ? "true" : "false" );
    }

    msg += "}";
    response.set_message( msg );
    return response;
}


HANDLER_RESULT<SavedDocumentResponse> API_HANDLER_PCB::handleSaveDocumentToString(
        const HANDLER_CONTEXT<SaveDocumentToString>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SavedDocumentResponse response;
    response.mutable_document()->CopyFrom( aCtx.Request.document() );

    CLIPBOARD_IO io;
    io.SetWriter(
        [&]( const wxString& aData )
        {
            response.set_contents( aData.ToUTF8() );
        } );

    io.SaveBoard( wxEmptyString, frame()->GetBoard(), nullptr );

    return response;
}


HANDLER_RESULT<SavedSelectionResponse> API_HANDLER_PCB::handleSaveSelectionToString(
        const HANDLER_CONTEXT<SaveSelectionToString>& aCtx )
{
    SavedSelectionResponse response;

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();
    PCB_SELECTION& selection = selectionTool->GetSelection();

    CLIPBOARD_IO io;
    io.SetWriter(
        [&]( const wxString& aData )
        {
            response.set_contents( aData.ToUTF8() );
        } );

    io.SetBoard( frame()->GetBoard() );
    io.SaveSelection( selection, false );

    return response;
}


HANDLER_RESULT<CreateItemsResponse> API_HANDLER_PCB::handleParseAndCreateItemsFromString(
        const HANDLER_CONTEXT<ParseAndCreateItemsFromString>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    wxString contents = wxString::FromUTF8( aCtx.Request.contents() );

    if( contents.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "contents string is empty" );
        return tl::unexpected( e );
    }

    // If the input doesn't start with (kicad_pcb or (footprint, wrap it in a board
    // container so the parser can handle individual items (tracks, vias, etc.)
    wxString toParse = contents.Trim();

    if( !toParse.StartsWith( wxT( "(kicad_pcb" ) ) && !toParse.StartsWith( wxT( "(footprint" ) ) )
    {
        // Build a wrapping kicad_pcb with layers and nets from the current board.
        // This mirrors what CLIPBOARD_IO::SaveSelection does for board-level items.
        STRING_FORMATTER wrapper;

        wrapper.Print( "(kicad_pcb (version %d) (generator \"api\") (generator_version \"1.0\")",
                       SEXPR_BOARD_FILE_VERSION );

        // Write layer definitions from current board
        wrapper.Print( "(layers" );

        for( PCB_LAYER_ID layer : board->GetEnabledLayers().CuStack() )
        {
            wrapper.Print( "(%d %s %s)", layer,
                           wrapper.Quotew( LSET::Name( layer ) ).c_str(),
                           LAYER::ShowType( board->GetLayerType( layer ) ) );
        }

        for( PCB_LAYER_ID layer : board->GetEnabledLayers().TechAndUserUIOrder() )
        {
            wrapper.Print( "(%d %s user)", layer,
                           wrapper.Quotew( LSET::Name( layer ) ).c_str() );
        }

        wrapper.Print( ")" );  // close layers

        // Net definitions
        wrapper.Print( "(net 0 \"\")" );

        for( const auto& [code, netinfo] : board->GetNetInfo().NetsByNetcode() )
        {
            if( code > 0 )
            {
                wrapper.Print( "(net %d %s)", code,
                               wrapper.Quotew( netinfo->GetNetname() ).c_str() );
            }
        }

        // Insert the user's content
        wrapper.Print( "%s", toParse.utf8_str().data() );
        wrapper.Print( ")" );  // close kicad_pcb

        toParse = wxString::FromUTF8( wrapper.GetString() );
    }

    // Parse the string
    PCB_IO_KICAD_SEXPR io;
    BOARD_ITEM* parsedItem = nullptr;

    try
    {
        parsedItem = io.Parse( toParse );
    }
    catch( const PARSE_ERROR& e )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "parse error: {}", e.What().ToStdString() ) );
        return tl::unexpected( err );
    }

    if( !parsedItem )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "parsing produced no items" );
        return tl::unexpected( e );
    }

    BOARD_COMMIT* commit = static_cast<BOARD_COMMIT*>( getCurrentCommit( aCtx.ClientName ) );
    CreateItemsResponse response;

    auto addItem = [&]( BOARD_ITEM* aItem )
    {
        aItem->SetParent( board );

        // Map nets from parsed board to real board
        if( auto* connItem = dynamic_cast<BOARD_CONNECTED_ITEM*>( aItem ) )
        {
            if( connItem->GetNet() )
            {
                NETINFO_ITEM* realNet = board->FindNet( connItem->GetNet()->GetNetname() );
                if( realNet )
                    connItem->SetNet( realNet );
                else
                    connItem->SetNet( board->FindNet( 0 ) );
            }
        }

        board->Add( aItem );

        if( commit )
            commit->Added( aItem );

        google::protobuf::Any itemMsg;
        aItem->Serialize( itemMsg );
        ItemCreationResult* result = response.add_created_items();
        result->mutable_status()->set_code( ItemStatusCode::ISC_OK );
        *result->mutable_item() = itemMsg;
    };

    if( parsedItem->Type() == PCB_T )
    {
        // Parsed a full board — extract all items
        BOARD* clipBoard = static_cast<BOARD*>( parsedItem );

        // Map nets
        clipBoard->MapNets( board );

        // Move footprints
        for( FOOTPRINT* fp : clipBoard->Footprints() )
        {
            clipBoard->Remove( fp );
            addItem( fp );
        }

        // Move tracks
        for( PCB_TRACK* track : clipBoard->Tracks() )
        {
            clipBoard->Remove( track );
            addItem( track );
        }

        // Move zones
        for( ZONE* zone : clipBoard->Zones() )
        {
            clipBoard->Remove( zone );
            addItem( zone );
        }

        // Move drawings (text, shapes, dimensions, etc.)
        for( BOARD_ITEM* item : clipBoard->Drawings() )
        {
            clipBoard->Remove( item );
            addItem( item );
        }

        delete clipBoard;
    }
    else if( parsedItem->Type() == PCB_FOOTPRINT_T )
    {
        // Single footprint
        addItem( parsedItem );
    }
    else
    {
        // Single item (shouldn't normally happen with wrapped parse)
        addItem( parsedItem );
    }

    frame()->GetCanvas()->Refresh();
    return response;
}


HANDLER_RESULT<BoardLayers> API_HANDLER_PCB::handleGetVisibleLayers(
        const HANDLER_CONTEXT<GetVisibleLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardLayers response;

    for( PCB_LAYER_ID layer : frame()->GetBoard()->GetVisibleLayers() )
        response.add_layers( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( layer ) );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetVisibleLayers(
        const HANDLER_CONTEXT<SetVisibleLayers>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    LSET visible;
    LSET enabled = frame()->GetBoard()->GetEnabledLayers();

    for( int layerIdx : aCtx.Request.layers() )
    {
        PCB_LAYER_ID layer =
                FromProtoEnum<PCB_LAYER_ID>( static_cast<board::types::BoardLayer>( layerIdx ) );

        if( enabled.Contains( layer ) )
            visible.set( layer );
    }

    frame()->GetBoard()->SetVisibleLayers( visible );
    frame()->GetAppearancePanel()->OnBoardChanged();
    frame()->GetCanvas()->SyncLayersVisibility( frame()->GetBoard() );
    frame()->Refresh();
    return Empty();
}


HANDLER_RESULT<BoardLayerResponse> API_HANDLER_PCB::handleGetActiveLayer(
        const HANDLER_CONTEXT<GetActiveLayer>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardLayerResponse response;
    response.set_layer(
            ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( frame()->GetActiveLayer() ) );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetActiveLayer(
        const HANDLER_CONTEXT<SetActiveLayer>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PCB_LAYER_ID layer = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );

    if( !frame()->GetBoard()->GetEnabledLayers().Contains( layer ) )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Layer {} is not a valid layer for the given board",
                                            magic_enum::enum_name( layer ) ) );
        return tl::unexpected( err );
    }

    frame()->SetActiveLayer( layer );
    return Empty();
}


HANDLER_RESULT<BoardEditorAppearanceSettings> API_HANDLER_PCB::handleGetBoardEditorAppearanceSettings(
        const HANDLER_CONTEXT<GetBoardEditorAppearanceSettings>& aCtx )
{
    BoardEditorAppearanceSettings reply;

    // TODO: might be nice to put all these things in one place and have it derive SERIALIZABLE

    const PCB_DISPLAY_OPTIONS& displayOptions = frame()->GetDisplayOptions();

    reply.set_inactive_layer_display( ToProtoEnum<HIGH_CONTRAST_MODE, InactiveLayerDisplayMode>(
            displayOptions.m_ContrastModeDisplay ) );
    reply.set_net_color_display(
            ToProtoEnum<NET_COLOR_MODE, NetColorDisplayMode>( displayOptions.m_NetColorMode ) );

    reply.set_board_flip( frame()->GetCanvas()->GetView()->IsMirroredX()
                                  ? BoardFlipMode::BFM_FLIPPED_X
                                  : BoardFlipMode::BFM_NORMAL );

    PCBNEW_SETTINGS* editorSettings = frame()->GetPcbNewSettings();

    reply.set_ratsnest_display( ToProtoEnum<RATSNEST_MODE, RatsnestDisplayMode>(
            editorSettings->m_Display.m_RatsnestMode ) );

    return reply;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetBoardEditorAppearanceSettings(
        const HANDLER_CONTEXT<SetBoardEditorAppearanceSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    PCB_DISPLAY_OPTIONS options = frame()->GetDisplayOptions();
    KIGFX::PCB_VIEW* view = frame()->GetCanvas()->GetView();
    PCBNEW_SETTINGS* editorSettings = frame()->GetPcbNewSettings();
    const BoardEditorAppearanceSettings& newSettings = aCtx.Request.settings();

    options.m_ContrastModeDisplay =
            FromProtoEnum<HIGH_CONTRAST_MODE>( newSettings.inactive_layer_display() );
    options.m_NetColorMode =
            FromProtoEnum<NET_COLOR_MODE>( newSettings.net_color_display() );

    bool flip = newSettings.board_flip() == BoardFlipMode::BFM_FLIPPED_X;

    if( flip != view->IsMirroredX() )
    {
        view->SetMirror( !view->IsMirroredX(), view->IsMirroredY() );
        view->RecacheAllItems();
    }

    editorSettings->m_Display.m_RatsnestMode =
            FromProtoEnum<RATSNEST_MODE>( newSettings.ratsnest_display() );

    frame()->SetDisplayOptions( options );
    frame()->GetCanvas()->GetView()->UpdateAllLayersColor();
    frame()->GetCanvas()->Refresh();

    return Empty();
}


HANDLER_RESULT<InjectDrcErrorResponse> API_HANDLER_PCB::handleInjectDrcError(
        const HANDLER_CONTEXT<InjectDrcError>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SEVERITY severity = FromProtoEnum<SEVERITY>( aCtx.Request.severity() );
    int      layer = severity == RPT_SEVERITY_WARNING ? LAYER_DRC_WARNING : LAYER_DRC_ERROR;
    int      code = severity == RPT_SEVERITY_WARNING ? DRCE_GENERIC_WARNING : DRCE_GENERIC_ERROR;

    std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( code );

    drcItem->SetErrorMessage( wxString::FromUTF8( aCtx.Request.message() ) );

    RC_ITEM::KIIDS ids;

    for( const auto& id : aCtx.Request.items() )
        ids.emplace_back( KIID( id.value() ) );

    if( !ids.empty() )
        drcItem->SetItems( ids );

    const auto& pos = aCtx.Request.position();
    VECTOR2I    position( static_cast<int>( pos.x_nm() ), static_cast<int>( pos.y_nm() ) );

    PCB_MARKER* marker = new PCB_MARKER( drcItem, position, layer );

    COMMIT* commit = getCurrentCommit( aCtx.ClientName );
    commit->Add( marker );
    commit->Push( wxS( "API injected DRC marker" ) );

    InjectDrcErrorResponse response;
    response.mutable_marker()->set_value( marker->GetUUID().AsStdString() );

    return response;
}
