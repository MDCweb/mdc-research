<?php
/**
 * @file
 */

function modterms_manage_suggestions_form() {
  // Grab available suggestions.
  $suggestions = modterms_suggestions_get_all();
  if (count($suggestions)) {
    $header = array('term' => 'Terms Needing Approval', 'node' => 'Attached To', 'uid' => 'Who Created');
    $options = array();
    foreach ($suggestions as $suggestion) {
      $node = node_load($suggestion->nid);
      $nodelink = l($node->title, 'node/' . $node->nid);
      $user = user_load($suggestion->uid);
      $options[$suggestion->mtid] = array('term' => $suggestion->name, 'node' => $nodelink, 'uid' => $user->name);
    }

    $form['table'] = array(
      '#type' => 'tableselect',
      '#header' => $header,
      '#options' => $options,
    );

    $form['controls'] = array(
      '#type' => 'fieldset',
      '#title' => t('Admin Controls'),
      '#description' => t('Approve or deny the selected suggestions.')
    );
    $form['controls']['operation'] = array(
      '#type' => 'select',
      '#title' => t('With selected'),
      '#options' => array(
        'new-term' => t('Approve as new terms'),
        'deny' => t('Deny and delete'),
      ),
      '#prefix' => '<div class="container-inline">'
    );

    $form['controls']['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Update'),
      '#suffix' => '</div>'
    );
  }
  else {
    $form['message'] = array(
      '#type' => 'markup',
      '#markup' => t('There are no terms waiting for approval.')
    );
  }

  return $form;
}

function modterms_manage_suggestions_form_submit($form, &$form_state) {
  $mtids = array_filter($form_state['values']['table']);
  //object_log('modterms manage', $mtids);
  foreach ($mtids as $mtid) {
    $suggestion = modterms_get_suggestion($mtid);
    //object_log('modterms got', $suggestion);
    switch ($form_state['values']['operation']) {
      case 'new-term':
        $term = modterms_suggestion_save_term($suggestion, $mtid);
        //object_log('modterms new term', $term);
        // Associate nodes that have made the suggestion with this term.
        modterms_association_register($suggestion[$mtid]->fname, $suggestion[$mtid]->nid, $term->tid);
        // Delete all instances of this suggestion from the unitag table (for
        // this vocabulary).
        modterms_suggestion_delete($mtid);
        modterms_notify_submitter('approved', $userid, $modterm);
        drupal_set_message(t('Suggested terms have been approved and added to the appropriate taxonomy.'));
        break;
      case 'deny':
        $userid = $suggestion[$mtid]->uid;
        $modterm = $suggestion[$mtid]->name;
        modterms_suggestion_delete($mtid);
        modterms_notify_submitter('denied', $userid, $modterm);
        drupal_set_message(t('Suggested terms have been deleted, and the user notified.'));
        break;
    }
  }
}

function modterms_notify_approver() {
  //Find submitters organic groups.
  $usergroup = og_get_groups_by_user();
  //object_log('modterms notify approver user group', $usergroup);
  //Find all group adminstrators (they should be the site managers) of the submitter's groups
  foreach ($usergroup as $value) {
    $adminmails = '';
    $adminids = modterms_get_all_admin_group($value);
    foreach ($adminids as $adminid) {
      $aduser = user_load($adminid);
      $adminmails .= $aduser->mail . ', ';
    }
    drupal_mail('modterms', 'modterms_submitted', $adminmails, language_default());
  }
}

function modterms_get_all_admin_group($gid) {
  $uids = array();
  $role_admin = array('administrator member'); //TO DO look at ogranic group roles
  $query = db_select('og_users_roles', 'ogur');
  $query->innerJoin('og_role', 'ogr', 'ogur.rid = ogr.rid');
  $rids = $query->fields('ogur', array('uid'))->condition('ogur.gid', $gid, '=')->condition('ogr.name', $role_admin, 'IN')->execute();
  foreach ($rids as $item) {
    $uids[] = $item->uid;
  }
  return $uids;
}

function modterms_notify_submitter($note, $userid, $modterm) {
  $user = user_load($userid);
  switch ($note) {
    case 'denied':
      $mail_key = 'modterms_deny';
      break;
    case 'approved':
      $mail_key = 'modterms_approve';
      break;
  }
  drupal_mail('modterms', $mail_key, $user->mail, user_preferred_language($user), array('account' => $user, 'term' => $modterm));
}

function modterms_mail($key, &$message, $params) {
  if ($key == 'modterms_deny') {
    $message['subject'] = t('Suggested term, @term_name, has been denied.', array('@term_name' => $params['term']));
    $message['body'][] = t('Your submitted term, @term_name, to the MDC research site has been denied by the site managers.', array('@term_name' => $params['term']));
    $message['body'][] = '';
    $message['body'][] = t('Feel free to suggest more terms by adding them to the appropriate content.');
  }
  if ($key == 'modterms_approve') {
    $message['subject'] = t('Suggested term, @term_name, has been accepted.', array('@term_name' => $params['term']));
    $message['body'][] = t('Your submitted term, @term_name, to the MDC research site has been accepted by the site managers.', array('@term_name' => $params['term']));
  }
if ($key == 'modterms_submitted') {
    $message['subject'] = t('A new term has been submitted for approval.');
    $message['body'][] = t('Please visit the <a href="@administer-page">term moderation page</a> to review new term submissions.', array('@administer-page' => url('admin/modterms/manage')));
  }
}

function modterms_get_suggestion($mtid) {
  $result = db_query("SELECT * FROM {modterms} m WHERE m.mtid = :mtid", array(':mtid' => $mtid));
  //TO DO: check to see if better way for result return
  $suggestion = array();
  foreach ($result as $row) {
    $suggestion[$row->mtid] = $row;
  }
  return $suggestion;
}

function modterms_suggestion_save_term($suggestion, $mtid) {
  $newterm = new stdClass();
  $newterm->vid = $suggestion[$mtid]->vid;
  $newterm->name = $suggestion[$mtid]->name;
  $newterm->parent = 0;
  taxonomy_term_save($newterm);

  $vid = $suggestion[$mtid]->vid; // Vocabulary ID
  $vocab = taxonomy_vocabulary_load($vid);
  $vocab_name = $vocab->machine_name;
  $savedterm = taxonomy_get_term_by_name($newterm->name, $vocab_name);
  //returns an array with key the tid, so simply grabbing the key
  $newterm->tid = key($savedterm);

  return ($newterm);
}

function modterms_association_register($fname, $nid, $tid) {
  //Load the node
  $node = node_load($nid);
  //Find the number of term reference field ($fname)
  $count = count($node->{$fname}[$node->language]);
  //Add new term to the node term reference field ($fname)
  $node->{$fname}[$node->language][$count]['tid'] = $tid;
  //Save the node
  node_save($node);
}

function modterms_suggestion_delete($mtid) {
  //Query to remove the row from the modterms table
  db_delete('modterms')->condition('mtid', $mtid)->execute();
}

function modterms_suggestions_get_all($pager = 25) {
  $result = db_query("SELECT * FROM {modterms} m WHERE m.mtid <> 0 ORDER BY m.name ASC");

  $suggestions = array();
  foreach ($result as $row) {
    $suggestions[$row->mtid] = $row;
  }

  return $suggestions;
}
