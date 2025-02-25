/****************************************************************************
 *   Aug 3 12:17:11 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
 ****************************************************************************/
 
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "config.h"
#include <TTGO.h>
#include "quickglui/quickglui.h"

#include "osmmap_app.h"
#include "osmmap_app_main.h"

#include "gui/mainbar/setup_tile/bluetooth_settings/bluetooth_message.h"
#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_styles.h"

#include "hardware/display.h"
#include "hardware/gpsctl.h"
#include "hardware/blectl.h"

#include "utils/osm_map/osm_map.h"
#include "utils/json_psram_allocator.h"

extern const uint8_t osm_server_json_start[] asm("_binary_src_utils_osm_map_osmtileserver_json_start");
extern const uint8_t osm_server_json_end[] asm("_binary_src_utils_osm_map_osmtileserver_json_end");

EventGroupHandle_t osmmap_event_handle = NULL;     /** @brief osm tile image update event queue */
TaskHandle_t _osmmap_download_Task;                /** @brief osm tile image update Task */
lv_task_t *osmmap_main_tile_task;                  /** @brief osm active/inactive task for show/hide user interface */

lv_obj_t *osmmap_app_main_tile = NULL;              /** @brief osm main tile obj */
lv_obj_t *osmmap_app_tile_img = NULL;               /** @brief osm tile image obj */
lv_obj_t *osmmap_app_pos_img = NULL;                /** @brief osm position point obj */
lv_obj_t *osmmap_lonlat_label = NULL;               /** @brief osm exit icon/button obj */
lv_obj_t *layers_btn = NULL;                          /** @brief osm exit icon/button obj */
lv_obj_t *layers_list = NULL;                       /** @brief osm style list box */
lv_obj_t *exit_btn = NULL;                          /** @brief osm exit icon/button obj */
lv_obj_t *zoom_in_btn = NULL;                       /** @brief osm zoom in icon/button obj */
lv_obj_t *zoom_out_btn = NULL;                      /** @brief osm zoom out icon/button obj */

lv_style_t osmmap_app_main_style;                   /** @brief osm main styte obj */
lv_style_t osmmap_app_label_style;                  /** @brief osm main styte obj */

static bool osmmap_app_active = false;              /** @brief osm app active/inactive flag, true means active */
static bool osmmap_block_return_maintile = false;   /** @brief osm block to maintile state store */
static bool osmmap_block_show_messages = false;     /** @brief osm show messages state store */
static bool osmmap_statusbar_force_dark_mode = false;  /** @brief osm statusbar force dark mode state store */

osm_location_t *osmmap_location = NULL;             /** @brief osm location obj */


LV_IMG_DECLARE(layers_dark_48px);
LV_IMG_DECLARE(exit_dark_48px);
LV_IMG_DECLARE(zoom_in_dark_48px);
LV_IMG_DECLARE(zoom_out_dark_48px);
LV_IMG_DECLARE(osm_64px);
LV_IMG_DECLARE(info_fail_16px);
LV_IMG_DECLARE(checked_dark_16px);
LV_IMG_DECLARE(unchecked_dark_16px);
LV_FONT_DECLARE(Ubuntu_12px);
LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_32px);

void osmmap_main_tile_update_task( lv_task_t * task );
void osmmap_update_request( void );
void osmmap_update_Task( void * pvParameters );
static void zoom_in_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void zoom_out_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void exit_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
static void osmmap_tile_server_event_cb( lv_obj_t * obj, lv_event_t event );
static void layers_btn_app_main_event_cb( lv_obj_t * obj, lv_event_t event );
void osmmap_update_map( osm_location_t *osmmap_location, double lon, double lat, uint32_t zoom );
bool osmmap_gpsctl_event_cb( EventBits_t event, void *arg );
void osmmap_add_tile_server_list( lv_obj_t *layers_list );
void osmmap_activate_cb( void );
void osmmap_hibernate_cb( void );

void osmmap_app_main_setup( uint32_t tile_num ) {

    osmmap_app_main_tile = mainbar_get_tile_obj( tile_num );

    lv_style_copy( &osmmap_app_main_style, ws_get_mainbar_style() );
    lv_obj_add_style( osmmap_app_main_tile, LV_OBJ_PART_MAIN, &osmmap_app_main_style );

    lv_style_copy( &osmmap_app_label_style, ws_get_mainbar_style() );
    lv_style_set_text_font( &osmmap_app_label_style, LV_OBJ_PART_MAIN, &Ubuntu_12px );
    lv_style_set_text_color(&osmmap_app_label_style, LV_OBJ_PART_MAIN, LV_COLOR_BLACK );

    lv_obj_t *osmmap_cont = lv_obj_create( osmmap_app_main_tile, NULL );
    lv_obj_set_size(osmmap_cont, lv_disp_get_hor_res( NULL ), lv_disp_get_ver_res( NULL ) );
    lv_obj_add_style( osmmap_cont, LV_OBJ_PART_MAIN, &osmmap_app_main_style );
    lv_obj_align( osmmap_cont, osmmap_app_main_tile, LV_ALIGN_IN_TOP_RIGHT, 0, 0 );

    osmmap_app_tile_img = lv_img_create( osmmap_cont, NULL );
    lv_obj_set_width( osmmap_app_tile_img, lv_disp_get_hor_res( NULL ) );
    lv_obj_set_height( osmmap_app_tile_img, lv_disp_get_ver_res( NULL ) );
    lv_img_set_src( osmmap_app_tile_img, osm_map_get_no_data_image() );
    lv_obj_align( osmmap_app_tile_img, osmmap_cont, LV_ALIGN_CENTER, 0, 0 );

    osmmap_app_pos_img = lv_img_create( osmmap_cont, NULL );
    lv_img_set_src( osmmap_app_pos_img, &info_fail_16px );
    lv_obj_align( osmmap_app_pos_img, osmmap_cont, LV_ALIGN_IN_TOP_LEFT, 120, 120 );
    lv_obj_set_hidden( osmmap_app_pos_img, true );

    osmmap_lonlat_label = lv_label_create( osmmap_cont, NULL );
    lv_obj_add_style( osmmap_lonlat_label, LV_OBJ_PART_MAIN, &osmmap_app_label_style );
    lv_obj_align( osmmap_lonlat_label, osmmap_cont, LV_ALIGN_IN_TOP_LEFT, 3, 0 );
    lv_label_set_text( osmmap_lonlat_label, "0 / 0" );

    layers_btn = lv_imgbtn_create( osmmap_cont, NULL);
    lv_imgbtn_set_src( layers_btn, LV_BTN_STATE_RELEASED, &layers_dark_48px);
    lv_imgbtn_set_src( layers_btn, LV_BTN_STATE_PRESSED, &layers_dark_48px);
    lv_imgbtn_set_src( layers_btn, LV_BTN_STATE_CHECKED_RELEASED, &layers_dark_48px);
    lv_imgbtn_set_src( layers_btn, LV_BTN_STATE_CHECKED_PRESSED, &layers_dark_48px);
    lv_obj_add_style( layers_btn, LV_IMGBTN_PART_MAIN, &osmmap_app_main_style );
    lv_obj_align( layers_btn, osmmap_cont, LV_ALIGN_IN_TOP_LEFT, 10, 10 + STATUSBAR_HEIGHT );
    lv_obj_set_event_cb( layers_btn, layers_btn_app_main_event_cb );

    exit_btn = lv_imgbtn_create( osmmap_cont, NULL);
    lv_imgbtn_set_src( exit_btn, LV_BTN_STATE_RELEASED, &exit_dark_48px);
    lv_imgbtn_set_src( exit_btn, LV_BTN_STATE_PRESSED, &exit_dark_48px);
    lv_imgbtn_set_src( exit_btn, LV_BTN_STATE_CHECKED_RELEASED, &exit_dark_48px);
    lv_imgbtn_set_src( exit_btn, LV_BTN_STATE_CHECKED_PRESSED, &exit_dark_48px);
    lv_obj_add_style( exit_btn, LV_IMGBTN_PART_MAIN, &osmmap_app_main_style );
    lv_obj_align( exit_btn, osmmap_cont, LV_ALIGN_IN_BOTTOM_LEFT, 10, -10 );
    lv_obj_set_event_cb( exit_btn, exit_osmmap_app_main_event_cb );

    zoom_in_btn = lv_imgbtn_create( osmmap_cont, NULL);
    lv_imgbtn_set_src( zoom_in_btn, LV_BTN_STATE_RELEASED, &zoom_in_dark_48px);
    lv_imgbtn_set_src( zoom_in_btn, LV_BTN_STATE_PRESSED, &zoom_in_dark_48px);
    lv_imgbtn_set_src( zoom_in_btn, LV_BTN_STATE_CHECKED_RELEASED, &zoom_in_dark_48px);
    lv_imgbtn_set_src( zoom_in_btn, LV_BTN_STATE_CHECKED_PRESSED, &zoom_in_dark_48px);
    lv_obj_add_style( zoom_in_btn, LV_IMGBTN_PART_MAIN, &osmmap_app_main_style );
    lv_obj_align( zoom_in_btn, osmmap_cont, LV_ALIGN_IN_TOP_RIGHT, -10, 10 + STATUSBAR_HEIGHT );
    lv_obj_set_event_cb( zoom_in_btn, zoom_in_osmmap_app_main_event_cb );

    zoom_out_btn = lv_imgbtn_create( osmmap_cont, NULL);
    lv_imgbtn_set_src( zoom_out_btn, LV_BTN_STATE_RELEASED, &zoom_out_dark_48px);
    lv_imgbtn_set_src( zoom_out_btn, LV_BTN_STATE_PRESSED, &zoom_out_dark_48px);
    lv_imgbtn_set_src( zoom_out_btn, LV_BTN_STATE_CHECKED_RELEASED, &zoom_out_dark_48px);
    lv_imgbtn_set_src( zoom_out_btn, LV_BTN_STATE_CHECKED_PRESSED, &zoom_out_dark_48px);
    lv_obj_add_style( zoom_out_btn, LV_IMGBTN_PART_MAIN, &osmmap_app_main_style );
    lv_obj_align( zoom_out_btn, osmmap_cont, LV_ALIGN_IN_BOTTOM_RIGHT, -10, -10 );
    lv_obj_set_event_cb( zoom_out_btn, zoom_out_osmmap_app_main_event_cb );

    layers_list = lv_list_create( osmmap_cont, NULL );
    lv_obj_set_size( layers_list, 160, 180 );
    lv_obj_align( layers_list, NULL, LV_ALIGN_IN_RIGHT_MID, 0, 0);
    osmmap_add_tile_server_list( layers_list );
    lv_obj_set_hidden( layers_list, true );

    mainbar_add_tile_activate_cb( tile_num, osmmap_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, osmmap_hibernate_cb );
    gpsctl_register_cb( GPSCTL_SET_APP_LOCATION | GPSCTL_UPDATE_LOCATION, osmmap_gpsctl_event_cb, "osm" );

    osmmap_location = osm_map_create_location_obj();
    osmmap_event_handle = xEventGroupCreate();
    osmmap_main_tile_task = lv_task_create( osmmap_main_tile_update_task, 250, LV_TASK_PRIO_MID, NULL );
}

/**
 * @brief when osm is active, this task get the use inactive time and hide
 * the statusbar and icon.
 */
void osmmap_main_tile_update_task( lv_task_t * task ) {
    /*
     * check if maintile alread initialized
     */
    if ( osmmap_app_active ) {
        if ( lv_disp_get_inactive_time( NULL ) > 5000 ) {
            lv_obj_set_hidden( layers_btn, true );
            lv_obj_set_hidden( exit_btn, true );
            lv_obj_set_hidden( zoom_in_btn, true );
            lv_obj_set_hidden( zoom_out_btn, true );
            statusbar_hide( true );
            statusbar_expand( false );
        }
        else {
            lv_obj_set_hidden( layers_btn, false );
            lv_obj_set_hidden( exit_btn, false );
            lv_obj_set_hidden( zoom_in_btn, false );
            lv_obj_set_hidden( zoom_out_btn, false );
            statusbar_hide( false );
        }
    }
}

bool osmmap_gpsctl_event_cb( EventBits_t event, void *arg ) {
    gps_data_t *gps_data = NULL;
    char lonlat[64] = "";
    
    switch ( event ) {
        case GPSCTL_SET_APP_LOCATION:
            /**
             * update location and tile map image on new location
             */
            gps_data = ( gps_data_t *)arg;
            osm_map_set_lon_lat( osmmap_location, gps_data->lon, gps_data->lat );
            snprintf( lonlat, sizeof( lonlat ), "%f° / %f°", gps_data->lat, gps_data->lon );
            lv_label_set_text( osmmap_lonlat_label, (const char*)lonlat );
            if ( osmmap_app_active )
                osmmap_update_request();
            break;
        case GPSCTL_UPDATE_LOCATION:
            /**
             * update location and tile map image on new location
             */
            gps_data = ( gps_data_t *)arg;
            osm_map_set_lon_lat( osmmap_location, gps_data->lon, gps_data->lat );
            snprintf( lonlat, sizeof( lonlat ), "%f° / %f°", gps_data->lat, gps_data->lon );
            lv_label_set_text( osmmap_lonlat_label, (const char*)lonlat );
            if ( osmmap_app_active )
                osmmap_update_request();
            break;
    }
    return( true );
}


void osmmap_update_request( void ) {
    /**
     * check if another osm tile image update is running
     */
    if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_TILE_IMAGE_REQUEST ) {
        return;
    }
    else {
        xEventGroupSetBits( osmmap_event_handle, OSM_APP_TILE_IMAGE_REQUEST );
    }
}

void osmmap_update_Task( void * pvParameters ) {
    log_i("start osm map tile background update task, heap: %d", ESP.getFreeHeap() );
    while( 1 ) {
        /**
         * check if a tile image update is requested
         */
        if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_TILE_IMAGE_REQUEST ) {
            /**
             * check if a tile image update is required and update them
             */
            if( osm_map_update( osmmap_location ) ) {
                if ( osm_map_get_tile_image( osmmap_location ) ) {
                    lv_img_set_src( osmmap_app_tile_img, osm_map_get_tile_image( osmmap_location ) );
                }
                lv_obj_align( osmmap_app_tile_img, lv_obj_get_parent( osmmap_app_tile_img ), LV_ALIGN_CENTER, 0 , 0 );
            }
            /**
             * update postion point on the tile image
             */
            lv_obj_align( osmmap_app_pos_img, lv_obj_get_parent( osmmap_app_pos_img ), LV_ALIGN_IN_TOP_LEFT, osmmap_location->tilex_pos - 8 , osmmap_location->tiley_pos - 8 );
            lv_obj_set_hidden( osmmap_app_pos_img, false );
            /**
             * clear update request flag
             */
            xEventGroupClearBits( osmmap_event_handle, OSM_APP_TILE_IMAGE_REQUEST );
        }
        /**
         * check if for a task exit request
         */
        if ( xEventGroupGetBits( osmmap_event_handle ) & OSM_APP_TASK_EXIT_REQUEST ) {
            xEventGroupClearBits( osmmap_event_handle, OSM_APP_TASK_EXIT_REQUEST );
            break;
        }
        /**
         * block this task for 125ms
         */
        vTaskDelay( 125 );
    }
    log_i("finsh osm map tile background update task, heap: %d", ESP.getFreeHeap() );
    vTaskDelete( NULL );    
}

static void exit_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):
            /**
             * exit to mainbar
             */
            mainbar_jump_to_maintile( LV_ANIM_OFF );
            break;
    }
}

static void zoom_in_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):   
            /**
             * increase zoom level
             */
            osm_map_zoom_in( osmmap_location );
            if ( osmmap_app_active )
                osmmap_update_request();
            break;
    }
}

static void zoom_out_osmmap_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):   
            /**
             * decrease zoom level
             */
            osm_map_zoom_out( osmmap_location );
            if ( osmmap_app_active )
                osmmap_update_request();
            break;
    }
}

static void layers_btn_app_main_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):
            if ( lv_obj_get_hidden( layers_list ) ) {
                lv_obj_set_hidden( layers_list, false );
            }
            else {
                lv_obj_set_hidden( layers_list, true );
            }
            break;
    }
}

static void osmmap_tile_server_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED: {
            SpiRamJsonDocument doc( strlen( (const char*)osm_server_json_start ) * 2 );
            DeserializationError error = deserializeJson( doc, (const char *)osm_server_json_start );

            if ( error ) {
                log_e("osm server list deserializeJson() failed: %s", error.c_str() );
            }
            else {
                const char *tile_server = doc[ lv_list_get_btn_text( obj ) ];
                log_i("new tile server url: %s", tile_server );
                osm_map_set_tile_server( osmmap_location, tile_server );
                osmmap_update_request();
            }
            doc.clear();
            lv_obj_set_hidden( layers_list, true );            
            break;
        }
    }
}

void osmmap_add_tile_server_list( lv_obj_t *layers_list ) {
    lv_obj_t * list_btn;

    SpiRamJsonDocument doc( strlen( (const char*)osm_server_json_start ) * 2 );
    DeserializationError error = deserializeJson( doc, (const char *)osm_server_json_start );

    if ( error ) {
        log_e("osm server list deserializeJson() failed: %s", error.c_str() );
    }
    else {
        JsonObject obj = doc.as<JsonObject>();
        for ( JsonPair p : obj ) {
            log_i("server: %s", p.key().c_str() );
            list_btn = lv_list_add_btn( layers_list, NULL, p.key().c_str() );
            lv_obj_set_event_cb( list_btn, osmmap_tile_server_event_cb );
        }        
    }
    doc.clear();
}

void osmmap_activate_cb( void ) {
    /**
     * save block show messages state
     */
    osmmap_block_show_messages = blectl_get_show_notification();
    /**
     * save black return to maintile state
     */
    osmmap_block_return_maintile = display_get_block_return_maintile();
    display_set_block_return_maintile( true );
    /**
     * save statusbar force dark mode state
     */
    osmmap_statusbar_force_dark_mode = statusbar_get_force_dark();
    statusbar_set_force_dark( true );
    /**
     * force redraw screen
     */
    lv_obj_invalidate( lv_scr_act() );
    statusbar_expand( true );
    statusbar_expand( false );
    /**
     * set osm app active
     */
    osmmap_app_active = true;
    /**
     * start background osm tile image update Task
     */
    xTaskCreate(    osmmap_update_Task,      /* Function to implement the task */
                    "osmmap update Task",    /* Name of the task */
                    5000,                            /* Stack size in words */
                    NULL,                            /* Task input parameter */
                    1,                               /* Priority of the task */
                    &_osmmap_download_Task );  /* Task handle. */

    osmmap_update_request();
    lv_img_cache_invalidate_src( osmmap_app_tile_img );
    log_i("osm layer list size: %d", strlen( (const char*)osm_server_json_start ) );
}

void osmmap_hibernate_cb( void ) {
    /**
     * restore back to maintile and status force dark mode
     */
    blectl_set_show_notification( osmmap_block_show_messages );
    display_set_block_return_maintile( osmmap_block_return_maintile );
    statusbar_set_force_dark( osmmap_statusbar_force_dark_mode );
    /**
     * set osm app inactive
     */
    osmmap_app_active = false;
    statusbar_hide( false );
    /**
     * stop background osm tile image update Task
     */
    xEventGroupSetBits( osmmap_event_handle, OSM_APP_TASK_EXIT_REQUEST );
}
