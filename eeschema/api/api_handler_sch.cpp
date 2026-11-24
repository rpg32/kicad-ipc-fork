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
#include <sch_pin.h>
#include <sch_field.h>
#include <eda_text.h>
#include <sch_line.h>
#include <sch_junction.h>
#include <tools/sch_line_wire_bus_tool.h>
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
#include <sch_reference_list.h>
#include <sch_screen.h>
#include <reporter.h>
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
    registerHandler<AnnotateSymbols, AnnotateSymbolsResponse>(
            &API_HANDLER_SCH::handleAnnotateSymbols );
    registerHandler<GetSheetGeometry, GetSheetGeometryResponse>(
            &API_HANDLER_SCH::handleGetSheetGeometry );
    registerHandler<GetTitleBlockInfo, kiapi::common::types::TitleBlockInfo>(
            &API_HANDLER_SCH::handleGetTitleBlockInfo );
    registerHandler<SetTitleBlockInfo, Empty>( &API_HANDLER_SCH::handleSetTitleBlockInfo );
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

    // The project, as the board editor reports it: with several schematics of the same
    // file name open (one per KiCad instance), the directory is what tells them apart.
    doc.mutable_project()->set_name( m_frame->Prj().GetProjectName().ToStdString() );
    doc.mutable_project()->set_path( m_frame->Prj().GetProjectDirectory().ToStdString() );

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


namespace
{
/**
 * The library symbol for @a aLibId, loaded the way symbol linking loads it when a schematic is
 * opened, or nullptr with the reason in @a aLoadError.
 *
 * Libraries load in the background after the editor opens (a minute on a busy machine). A
 * request that arrives before its library is in was told "library symbol not found" about a
 * symbol that exists. Make sure that one library is in: wait while a background worker is
 * parsing it right now (two parsers on one library's cache is not safe), otherwise load it
 * here.
 */
LIB_SYMBOL* loadLibrarySymbol( SCH_EDIT_FRAME* aFrame, const LIB_ID& aLibId, wxString& aLoadError )
{
    SYMBOL_LIBRARY_ADAPTER* adapter = PROJECT_SCH::SymbolLibAdapter( &aFrame->Prj() );

    if( !adapter )
        return nullptr;

    try
    {
        const wxString nickname = aLibId.GetLibNickname();

        for( int waited = 0; waited < 30000; waited += 20 )
        {
            std::optional<LIB_STATUS> state = adapter->GetLibraryStatus( nickname );

            if( !state || state->load_status != LOAD_STATUS::LOADING )
                break;

            wxMilliSleep( 20 );
        }

        if( !adapter->IsLibraryLoaded( nickname ) )
        {
            std::optional<LIB_STATUS> loaded = adapter->LoadOne( nickname );

            if( loaded && loaded->load_status != LOAD_STATUS::LOADED )
            {
                aLoadError = loaded->error ? loaded->error->message
                                           : wxString( wxT( "the library could not be loaded" ) );
            }
        }

        return adapter->LoadSymbol( aLibId );
    }
    catch( const IO_ERROR& ioe )
    {
        aLoadError = ioe.What();
        return nullptr;
    }
}


/// Says which it is: a library that failed to load, or a symbol that library does not have.
std::string libSymbolNotFoundMessage( const LIB_ID& aLibId, const wxString& aLoadError )
{
    if( aLoadError.IsEmpty() )
        return fmt::format( "library symbol '{}' not found", std::string( aLibId.Format() ) );

    return fmt::format( "library symbol '{}' not found: library '{}': {}",
                        std::string( aLibId.Format() ), std::string( aLibId.GetLibNickname() ),
                        aLoadError.ToStdString() );
}
} // namespace


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
            wxString    loadError;
            LIB_SYMBOL* libSymbol = loadLibrarySymbol( m_frame, libId, loadError );

            if( !libSymbol )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_TYPE );

                status.set_error_message( libSymbolNotFoundMessage( libId, loadError ) );

                aItemHandler( status, anyItem );
                continue;
            }

            // Extract position
            VECTOR2I pos = kiapi::common::UnpackVector2( symMsg.position() );

            // Extract unit and body style. Both are 1-based; 0 is the proto default for "not
            // given". A body style of 0 was passed straight through before, and pins (body style
            // 1) are skipped for connection points when it differs from the symbol's, so a pin
            // landing mid-wire never counted as connected and cleanup deleted its junction.
            int unit = symMsg.unit() > 0 ? symMsg.unit() : 1;
            int bodyStyle = symMsg.body_style() > 0 ? symMsg.body_style() : BODY_STYLE::BASE;

            if( bodyStyle > std::max( 1, libSymbol->GetBodyStyleCount() ) )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_DATA );
                status.set_error_message( fmt::format(
                        "body style {} out of range: '{}' has {} body style(s)", bodyStyle,
                        std::string( libId.Format() ), libSymbol->GetBodyStyleCount() ) );
                aItemHandler( status, anyItem );
                continue;
            }

            if( unit > std::max( 1, libSymbol->GetUnitCount() ) )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_DATA );
                status.set_error_message( fmt::format( "unit {} out of range: '{}' has {} unit(s)",
                                                       unit, std::string( libId.Format() ),
                                                       libSymbol->GetUnitCount() ) );
                aItemHandler( status, anyItem );
                continue;
            }

            // Construct symbol the same way as interactive placement, *with* the sheet it is
            // placed on. The unit and reference of a placed symbol live in its per-sheet
            // instance data, and the saver writes the unit from that instance. Passing nullptr
            // here left the symbol with no instance, so every save wrote the default (unit 1)
            // and dropped the requested unit (multi-unit parts all collapsed to unit A).
            const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
            auto symbol = std::make_unique<SCH_SYMBOL>( *libSymbol, libId, &sheet,
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

            // The Reference field override above changed only the field text; record it (and
            // the unit) in the sheet instance, which is what is saved and what annotation reads.
            symbol->SetRef( &sheet, symbol->GetField( FIELD_T::REFERENCE )->GetText() );
            symbol->SetUnitSelection( &sheet, unit );

            // Apply exclusion flags
            symbol->SetExcludedFromBOM( symMsg.exclude_from_bom() );
            symbol->SetExcludedFromBoard( symMsg.exclude_from_board() );
            symbol->SetDNP( symMsg.dnp() );

            // Place the fields as interactive placement does: Reference and Value clear of the
            // body for this orientation. The library's field positions, merely turned with the
            // symbol, put the texts on or against the body of a rotated part.
            if( m_frame->eeconfig()->m_AutoplaceFields.enable )
                symbol->AutoplaceFields( nullptr, AUTOPLACE_AUTO );

            // Add flattened lib symbol to screen cache (needed for file save)
            std::unique_ptr<LIB_SYMBOL> flatSymbol = libSymbol->Flatten();
            flatSymbol->SetParent();
            screen->AddLibSymbol( new LIB_SYMBOL( *flatSymbol ) );

            // Serialize result back and commit
            google::protobuf::Any newItem;
            status.set_code( ItemStatusCode::ISC_OK );
            symbol->Serialize( newItem );
            commit->Add( symbol.release(), screen );

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

            aItemHandler( status, newItem );
            continue;
        }

        // A symbol's field updated on its own: where it sits, whether it shows, and its text.
        // Fields are children of their symbol, not items of the screen, so they are found
        // through the symbols.  (Updating the whole symbol moves its fields along with it and
        // deliberately takes only their text, so there was no way to place a field.)
        if( !aCreate && *type == SCH_FIELD_T )
        {
            kiapi::schematic::types::SchematicField fieldMsg;

            if( !anyItem.UnpackTo( &fieldMsg ) )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( "could not unpack SchematicField from request" );
                return tl::unexpected( e );
            }

            const KIID  fieldId( fieldMsg.id().value() );
            SCH_SYMBOL* owner = nullptr;
            SCH_FIELD*  target = nullptr;

            for( SCH_ITEM* candidate : screen->Items().OfType( SCH_SYMBOL_T ) )
            {
                SCH_SYMBOL* candidateSymbol = static_cast<SCH_SYMBOL*>( candidate );

                for( SCH_FIELD& field : candidateSymbol->GetFields() )
                {
                    if( field.m_Uuid == fieldId )
                    {
                        owner = candidateSymbol;
                        target = &field;
                        break;
                    }
                }

                if( target )
                    break;
            }

            if( !target )
            {
                status.set_code( ItemStatusCode::ISC_NONEXISTENT );
                status.set_error_message( fmt::format( "no symbol on this sheet has a field with UUID {}",
                                                       fieldId.AsStdString() ) );
                aItemHandler( status, anyItem );
                continue;
            }

            const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
            const wxString        variant = m_frame->Schematic().GetCurrentVariant();
            const wxString        text = wxString::FromUTF8( fieldMsg.text() );

            commit->Modify( owner, screen );

            if( target->GetText() != text )
            {
                // Through the same setters as a whole-symbol update: reference and value live
                // in the per-sheet instance.
                if( target->GetId() == FIELD_T::REFERENCE )
                    owner->SetRef( &sheet, text );
                else if( target->GetId() == FIELD_T::VALUE )
                    owner->SetValueFieldText( text, &sheet, variant );
                else
                    target->SetText( text );

                owner->SyncOtherUnits( sheet, *static_cast<SCH_COMMIT*>( commit ), nullptr, variant );
            }

            if( fieldMsg.has_position() )
            {
                target->SetPosition( kiapi::common::UnpackVector2( fieldMsg.position() ) );

                // Placed on purpose: field autoplacement must not put it back.
                owner->SetFieldsAutoplaced( AUTOPLACE_NONE );
            }

            target->SetVisible( fieldMsg.visible() );

            google::protobuf::Any newItem;
            status.set_code( ItemStatusCode::ISC_OK );
            target->Serialize( newItem );
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
            // A junction is placed the way SCH_LINE_WIRE_BUS_TOOL::AddJunction places one: on
            // the bus layer when it sits on a bus, and breaking every wire that passes through
            // it so the point becomes real wire endpoints. Added bare, a junction on a wire's
            // mid-span did not connect anything there, and the commit's cleanup could delete it.
            std::optional<VECTOR2I> junctionPos;

            if( item->Type() == SCH_JUNCTION_T )
            {
                SCH_JUNCTION* junction = static_cast<SCH_JUNCTION*>( item.get() );
                junctionPos = junction->GetPosition();

                if( screen->GetBus( *junctionPos ) )
                    junction->SetLayer( LAYER_BUS_JUNCTION );
            }

            item->Serialize( newItem );
            commit->Add( item.release(), screen );

            if( junctionPos )
            {
                if( SCH_LINE_WIRE_BUS_TOOL* lwbTool =
                            m_frame->GetToolManager()->GetTool<SCH_LINE_WIRE_BUS_TOOL>() )
                {
                    lwbTool->BreakSegments( static_cast<SCH_COMMIT*>( commit ), *junctionPos,
                                            screen );
                }
            }
        }
        else
        {
            EDA_ITEM* edaItem = itemUuidMap[item->m_Uuid];

            if( SCH_ITEM* schItem = dynamic_cast<SCH_ITEM*>( edaItem ) )
            {
                if( schItem->Type() == SCH_SYMBOL_T )
                {
                    // For symbols, apply updates field-by-field to preserve library graphics
                    // data, but through the same setters the Symbol Properties dialog uses.
                    // The reference and unit of a placed symbol live in its per-sheet instance
                    // (that is what the saver writes), so a bare field SetText / SetUnit changed
                    // only the on-screen copy and was reverted by save.
                    SCH_SYMBOL* origSym = static_cast<SCH_SYMBOL*>( schItem );
                    SCH_SYMBOL* newSym = static_cast<SCH_SYMBOL*>( item.get() );
                    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
                    const wxString variant = m_frame->Schematic().GetCurrentVariant();
                    int unit = newSym->GetUnit() > 0 ? newSym->GetUnit() : origSym->GetUnit();

                    // Another library identifier is a request to change the symbol to that
                    // library symbol, as the Change Symbols dialog does for one symbol: body
                    // and pins come from the new library symbol, the placed symbol keeps its
                    // position, unit and field texts.
                    const LIB_ID                newLibId = newSym->GetLibId();
                    std::unique_ptr<LIB_SYMBOL> replacement;

                    if( newLibId.IsValid() && newLibId != origSym->GetLibId() )
                    {
                        wxString    loadError;
                        LIB_SYMBOL* libSymbol = loadLibrarySymbol( m_frame, newLibId, loadError );

                        if( !libSymbol )
                        {
                            status.set_code( ItemStatusCode::ISC_INVALID_DATA );
                            status.set_error_message( libSymbolNotFoundMessage( newLibId, loadError ) );
                            aItemHandler( status, anyItem );
                            continue;
                        }

                        replacement = libSymbol->Flatten();
                    }

                    const int unitCount = replacement ? replacement->GetUnitCount()
                                                      : origSym->GetUnitCount();

                    if( unit > std::max( 1, unitCount ) )
                    {
                        status.set_code( ItemStatusCode::ISC_INVALID_DATA );
                        status.set_error_message( fmt::format(
                                "unit {} out of range: {} has {} unit(s)", unit,
                                replacement ? "'" + std::string( newLibId.Format() ) + "'"
                                            : "symbol " + origSym->m_Uuid.AsStdString(),
                                unitCount ) );
                        aItemHandler( status, anyItem );
                        continue;
                    }

                    if( replacement )
                    {
                        // Off the screen while its library symbol changes, as the dialog does,
                        // so the screen does not keep the old library symbol for it.
                        screen->Remove( origSym );
                        commit->Modified( origSym, origSym->Clone(), screen );

                        origSym->SetLibId( newLibId );
                        origSym->SetLibSymbol( replacement.release() );
                        origSym->SetSchSymbolLibraryName( wxEmptyString );

                        // An alternate pin function the new symbol does not offer is dropped.
                        for( SCH_PIN* pin : origSym->GetPins( &sheet ) )
                        {
                            if( !pin->GetAlt().IsEmpty()
                                && pin->GetAlternates().count( pin->GetAlt() ) == 0 )
                            {
                                pin->SetAlt( wxEmptyString );
                            }
                        }

                        screen->Append( origSym );
                    }
                    else
                    {
                        commit->Modify( origSym, screen );
                    }

                    const int oldOrientation = origSym->GetOrientation();

                    origSym->SetPosition( newSym->GetPosition() );
                    origSym->SetOrientation( newSym->GetOrientation() );

                    // Turning or mirroring a symbol re-places its automatically placed fields,
                    // as the editor's own rotate does. Fields a user or a client positioned
                    // (AUTOPLACE_NONE) stay where they were put.
                    if( origSym->GetOrientation() != oldOrientation
                        && m_frame->eeconfig()->m_AutoplaceFields.enable )
                    {
                        AUTOPLACE_ALGO fieldsAutoplaced = origSym->GetFieldsAutoplaced();

                        if( fieldsAutoplaced == AUTOPLACE_AUTO || fieldsAutoplaced == AUTOPLACE_MANUAL )
                            origSym->AutoplaceFields( screen, fieldsAutoplaced );
                    }
                    origSym->SetUnitSelection( &sheet, unit );
                    origSym->SetUnit( unit );
                    // 0 = not given: keep the current body style (and repair a 0 left by
                    // symbols created over the API before body style defaulted to 1).
                    if( newSym->GetBodyStyle() > 0 )
                        origSym->SetBodyStyle( newSym->GetBodyStyle() );
                    else if( origSym->GetBodyStyle() <= 0 )
                        origSym->SetBodyStyle( BODY_STYLE::BASE );

                    // Update fields from the new item, matching by name
                    for( SCH_FIELD& newField : newSym->GetFields() )
                    {
                        SCH_FIELD* origField = origSym->GetField( newField.GetName() );

                        if( !origField )
                            continue;

                        if( origField->GetId() == FIELD_T::REFERENCE )
                            origSym->SetRef( &sheet, newField.GetText() );
                        else if( origField->GetId() == FIELD_T::VALUE )
                            origSym->SetValueFieldText( newField.GetText(), &sheet, variant );
                        else
                            origField->SetText( newField.GetText() );
                    }

                    // Keep value, footprint and the other shared fields, exclusion flags and
                    // alternate pin assignments in sync across the units of a multi-unit
                    // part, exactly as every interactive edit path does. Without this an
                    // update of U2's footprint changed only the one unit it was sent for.
                    origSym->SyncOtherUnits( sheet, *static_cast<SCH_COMMIT*>( commit ), nullptr,
                                             variant );

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
        }

        aItemHandler( status, newItem );
    }

    // Push once, after every item. Pushing inside the loop freed the commit (pushCurrentCommit
    // erases it) while `commit` still pointed at it, so the second item of any multi-item
    // CreateItems/UpdateItems wrote through a dangling pointer and crashed eeschema. Same
    // shape as API_HANDLER_PCB, and one undo step per request.
    if( !m_activeClients.count( aClientName ) )
    {
        pushCurrentCommit( aClientName, aCreate ? _( "Added items via API" )
                                                : _( "Modified items via API" ) );
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


namespace
{
/// Collects what the annotation pass reports: errors are kept, the rest is dropped (the
/// response lists the changed references itself).
class ANNOTATE_API_REPORTER : public REPORTER
{
public:
    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        if( aSeverity == RPT_SEVERITY_ERROR )
            m_errors.push_back( aText );

        return REPORTER::Report( aText, aSeverity );
    }

    std::vector<wxString> m_errors;
};


/// Reference of every non-power symbol instance, keyed by sheet path + symbol id.
struct ANNOTATION_SNAPSHOT
{
    struct ENTRY
    {
        KIID     id;
        wxString sheet;
        wxString ref;
        bool     annotated;
    };

    std::map<wxString, ENTRY> entries;

    explicit ANNOTATION_SNAPSHOT( SCHEMATIC& aSchematic )
    {
        for( const SCH_SHEET_PATH& path : aSchematic.Hierarchy() )
        {
            for( SCH_ITEM* item : path.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
            {
                SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

                if( symbol->IsPower() )
                    continue;

                wxString ref = symbol->GetRef( &path );

                if( symbol->GetUnitCount() > 1 )
                    ref << symbol->SubReference( symbol->GetUnitSelection( &path ) );

                KIID_PATH key = path.Path();
                key.push_back( symbol->m_Uuid );

                entries[key.AsString()] = { symbol->m_Uuid, path.PathHumanReadable( false ), ref,
                                            symbol->IsAnnotated( &path ) };
            }
        }
    }
};
} // namespace


HANDLER_RESULT<kiapi::schematic::commands::AnnotateSymbolsResponse>
API_HANDLER_SCH::handleAnnotateSymbols(
        const HANDLER_CONTEXT<kiapi::schematic::commands::AnnotateSymbols>& aCtx )
{
    using namespace kiapi::schematic::commands;

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    ANNOTATE_SCOPE_T scope = ANNOTATE_ALL;

    switch( aCtx.Request.scope() )
    {
    case AnnotateScope::ASC_CURRENT_SHEET: scope = ANNOTATE_CURRENT_SHEET; break;
    case AnnotateScope::ASC_SELECTION:     scope = ANNOTATE_SELECTION;     break;
    default:                               scope = ANNOTATE_ALL;           break;
    }

    ANNOTATE_ORDER_T order = aCtx.Request.order() == AnnotateOrder::AOR_Y_POSITION ? SORT_BY_Y_POSITION
                                                                                   : SORT_BY_X_POSITION;
    ANNOTATE_ALGO_T algo = INCREMENTAL_BY_REF;

    switch( aCtx.Request.numbering() )
    {
    case AnnotateNumbering::ANM_SHEET_X_100:  algo = SHEET_NUMBER_X_100;  break;
    case AnnotateNumbering::ANM_SHEET_X_1000: algo = SHEET_NUMBER_X_1000; break;
    default:                                  algo = INCREMENTAL_BY_REF;  break;
    }

    const bool reset = aCtx.Request.reset_existing();
    const bool recursive = scope == ANNOTATE_ALL || aCtx.Request.recursive();
    const int  startNumber = std::max( 1, aCtx.Request.start_number() );

    ANNOTATION_SNAPSHOT   before( m_frame->Schematic() );
    ANNOTATE_API_REPORTER reporter;
    SCH_COMMIT            commit( m_frame );

    // The same call, with the same fixed arguments, as the Annotate Schematic dialog makes.
    m_frame->AnnotateSymbols( &commit, scope, order, algo, recursive, startNumber, reset,
                              reset && aCtx.Request.regroup_units(), true, reporter,
                              SYMBOL_FILTER_NON_POWER );

    commit.Push( _( "Annotate" ) );

    ANNOTATION_SNAPSHOT     after( m_frame->Schematic() );
    AnnotateSymbolsResponse response;
    int                     unannotated = 0;

    for( const auto& [key, now] : after.entries )
    {
        if( !now.annotated )
            unannotated++;

        auto was = before.entries.find( key );

        if( was != before.entries.end() && was->second.ref == now.ref )
            continue;

        AnnotatedSymbol* changed = response.add_changed();
        changed->mutable_id()->set_value( now.id.AsStdString() );
        changed->set_sheet_path( now.sheet.ToStdString() );
        changed->set_old_reference( was != before.entries.end() ? was->second.ref.ToStdString()
                                                                : std::string() );
        changed->set_new_reference( now.ref.ToStdString() );
    }

    response.set_unannotated( unannotated );

    for( const wxString& error : reporter.m_errors )
        response.add_errors( error.ToStdString() );

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetSheetGeometryResponse>
API_HANDLER_SCH::handleGetSheetGeometry(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetSheetGeometry>& aCtx )
{
    using namespace kiapi::schematic::commands;

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    std::set<KIID> wanted;

    for( const kiapi::common::types::KIID& id : aCtx.Request.symbols() )
        wanted.insert( KIID( id.value() ) );

    SCH_SCREEN*              screen = m_frame->GetScreen();
    const SCH_SHEET_PATH&    sheet = m_frame->GetCurrentSheet();
    GetSheetGeometryResponse response;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() == SCH_SYMBOL_T )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( !wanted.empty() && !wanted.count( symbol->m_Uuid ) )
                continue;

            SymbolGeometry* out = response.add_symbols();
            out->mutable_id()->set_value( symbol->m_Uuid.AsStdString() );
            out->set_reference( symbol->GetRef( &sheet ).ToUTF8().data() );
            out->set_unit( symbol->GetUnitSelection( &sheet ) );
            out->set_power( symbol->IsPower() );
            kiapi::common::PackBox2( *out->mutable_body(), symbol->GetBodyBoundingBox() );
            kiapi::common::PackBox2( *out->mutable_body_and_pins(),
                                     symbol->GetBodyAndPinsBoundingBox() );

            for( SCH_PIN* pin : symbol->GetPins( &sheet ) )
            {
                PinGeometry* p = out->add_pins();
                p->set_number( pin->GetShownNumber().ToUTF8().data() );
                p->set_name( pin->GetShownName().ToUTF8().data() );
                kiapi::common::PackVector2( *p->mutable_position(), pin->GetPosition() );
                p->set_electrical_type( pin->GetCanonicalElectricalTypeName().ToUTF8().data() );
                p->set_visible( pin->IsVisible() );
                p->set_alternate( pin->GetAlt().ToUTF8().data() );

                for( const auto& [name, alt] : pin->GetAlternates() )
                    p->add_alternates( name.ToUTF8().data() );
            }

            for( SCH_FIELD& field : symbol->GetFields() )
            {
                FieldGeometry* f = out->add_fields();
                f->mutable_id()->set_value( field.m_Uuid.AsStdString() );
                f->set_name( field.GetName().ToUTF8().data() );
                f->set_text( field.GetShownText( &sheet, false ).ToUTF8().data() );
                f->set_visible( field.IsVisible() );
                kiapi::common::PackBox2( *f->mutable_box(), field.GetBoundingBox() );
            }

            continue;
        }

        if( !wanted.empty() )
            continue;

        switch( item->Type() )
        {
        case SCH_LABEL_T:
        case SCH_GLOBAL_LABEL_T:
        case SCH_HIER_LABEL_T:
        case SCH_DIRECTIVE_LABEL_T:
        case SCH_TEXT_T:
        case SCH_TEXTBOX_T:
        case SCH_SHEET_T:
        case SCH_JUNCTION_T:
        case SCH_NO_CONNECT_T:
        case SCH_SHAPE_T:
        case SCH_BITMAP_T:
        case SCH_TABLE_T:
        {
            ItemGeometry* out = response.add_items();
            out->mutable_id()->set_value( item->m_Uuid.AsStdString() );
            out->set_type( ToProtoEnum<KICAD_T, kiapi::common::types::KiCadObjectType>(
                    item->Type() ) );
            kiapi::common::PackBox2( *out->mutable_box(), item->GetBoundingBox() );

            if( EDA_TEXT* text = dynamic_cast<EDA_TEXT*>( item ) )
                out->set_text( text->GetShownText( false ).ToUTF8().data() );

            break;
        }

        default:
            break;
        }
    }

    return response;
}


// The title block of the sheet shown.  Each sheet of a schematic has its own.
HANDLER_RESULT<kiapi::common::types::TitleBlockInfo> API_HANDLER_SCH::handleGetTitleBlockInfo(
        const HANDLER_CONTEXT<commands::GetTitleBlockInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    const TITLE_BLOCK&                   block = m_frame->GetScreen()->GetTitleBlock();
    kiapi::common::types::TitleBlockInfo response;

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


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetTitleBlockInfo(
        const HANDLER_CONTEXT<commands::SetTitleBlockInfo>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( !aCtx.Request.has_title_block() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "SetTitleBlockInfo requires title_block" );
        return tl::unexpected( e );
    }

    const kiapi::common::types::TitleBlockInfo& request = aCtx.Request.title_block();
    SCH_SCREEN*                                 screen = m_frame->GetScreen();
    TITLE_BLOCK                                 block = screen->GetTitleBlock();

    block.SetTitle( wxString::FromUTF8( request.title() ) );
    block.SetDate( wxString::FromUTF8( request.date() ) );
    block.SetRevision( wxString::FromUTF8( request.revision() ) );
    block.SetCompany( wxString::FromUTF8( request.company() ) );
    block.SetComment( 0, wxString::FromUTF8( request.comment1() ) );
    block.SetComment( 1, wxString::FromUTF8( request.comment2() ) );
    block.SetComment( 2, wxString::FromUTF8( request.comment3() ) );
    block.SetComment( 3, wxString::FromUTF8( request.comment4() ) );
    block.SetComment( 4, wxString::FromUTF8( request.comment5() ) );
    block.SetComment( 5, wxString::FromUTF8( request.comment6() ) );
    block.SetComment( 6, wxString::FromUTF8( request.comment7() ) );
    block.SetComment( 7, wxString::FromUTF8( request.comment8() ) );
    block.SetComment( 8, wxString::FromUTF8( request.comment9() ) );

    screen->SetTitleBlock( block );
    m_frame->OnModify();
    m_frame->HardRedraw();

    return Empty();
}
