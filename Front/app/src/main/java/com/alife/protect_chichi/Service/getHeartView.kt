package com.alife.protect_chichi.Service


interface getHeartView {
    fun onSuccess(result: GetHeartResponse)
    fun onFailure(code:Int, message:String)

    fun onSendSignalSuccess(result: GetHeartResponse)
    fun onSendSignalFailure(code:Int, message:String)

    fun onSendFoodTimeSuccess(result: GetHeartResponse)
    fun onSendFoodTimeFailure(code:Int, message:String)
}